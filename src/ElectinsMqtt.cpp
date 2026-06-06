/*
 * ElectinsMqtt.cpp — Internal MQTT 3.1.1 Engine (v3, single-owner + outbox)
 *
 * Lihat ElectinsMqtt.h untuk penjelasan model eksekusi.
 *
 * Aturan emas implementasi ini:
 *   - Socket (_tcp) HANYA disentuh dari loop() (dan helper yang dipanggilnya).
 *   - publish()/subscribe()/unsubscribe() hanya menulis ke outbox (mutex).
 *   - connected() hanya membaca flag _state.
 */

#include "ElectinsMqtt.h"
#include <string.h>
#include <stdio.h>

// ─── Constructor / Destructor ─────────────────────────────────────────────────

ElectinsMqtt::ElectinsMqtt() {
    memset(_pending,  0, sizeof(_pending));
    memset(_outbox,   0, sizeof(_outbox));
    memset(_txScratch,0, sizeof(_txScratch));
    memset(_rxBuf,    0, sizeof(_rxBuf));
}

ElectinsMqtt::~ElectinsMqtt() {
    if (_client)    { delete _client;    _client    = nullptr; }
    if (_secureCli) { delete _secureCli; _secureCli = nullptr; }
    _tcp = nullptr;
#if defined(ESP32)
    if (_mtx) { vSemaphoreDelete(_mtx); _mtx = nullptr; }
#endif
}

void ElectinsMqtt::begin() {
#if defined(ESP32)
    if (!_mtx) _mtx = xSemaphoreCreateMutex();
#endif
}

// ─── Mutex ────────────────────────────────────────────────────────────────────

void ElectinsMqtt::_lock() {
#if defined(ESP32)
    if (_mtx) xSemaphoreTake(_mtx, portMAX_DELAY);
#endif
}

void ElectinsMqtt::_unlock() {
#if defined(ESP32)
    if (_mtx) xSemaphoreGive(_mtx);
#endif
}

// ─── Konfigurasi ──────────────────────────────────────────────────────────────

void ElectinsMqtt::setServer(const char* host, uint16_t port) {
    if (!host) return;
    strncpy(_host, host, sizeof(_host) - 1);
    _host[sizeof(_host) - 1] = '\0';
    _port = port;
}

void ElectinsMqtt::setClientId(const char* id) {
    if (!id) return;
    strncpy(_clientId, id, sizeof(_clientId) - 1);
    _clientId[sizeof(_clientId) - 1] = '\0';
}

void ElectinsMqtt::setCredentials(const char* user, const char* pass) {
    if (user) { strncpy(_user, user, sizeof(_user) - 1); _user[sizeof(_user)-1] = '\0'; }
    if (pass) { strncpy(_pass, pass, sizeof(_pass) - 1); _pass[sizeof(_pass)-1] = '\0'; }
}

void ElectinsMqtt::setKeepAlive(uint16_t s)          { _keepAliveSec = (s > 0) ? s : 15; }
void ElectinsMqtt::setReconnectInterval(uint16_t s)  { _reconnectSec = (s > 0) ? s : 1; }
void ElectinsMqtt::setSecure(bool e)                 { _secure   = e; }
void ElectinsMqtt::setInsecure(bool i)               { _insecure = i; }

void ElectinsMqtt::setWill(const char* topic, const char* payload,
                            bool retain, uint8_t qos) {
    if (!topic || !payload) return;
    strncpy(_willTopic,   topic,   sizeof(_willTopic)   - 1);
    strncpy(_willPayload, payload, sizeof(_willPayload) - 1);
    _willTopic[sizeof(_willTopic)-1]     = '\0';
    _willPayload[sizeof(_willPayload)-1] = '\0';
    _willRetain = retain;
    _willQos    = qos & 0x01;
    _hasWill    = true;
}

// ─── LOOP — single owner, dipanggil dari task/scheduled function ──────────────

void ElectinsMqtt::loop() {
    // Permintaan disconnect dari pengguna
    if (_wantDisconnect) {
        _wantDisconnect = false;
        if (_state == EMQTT_CONNECTED) {
            _sendDisconnect();
            _onDisconnected();
        }
        return;
    }

    if (_state == EMQTT_DISCONNECTED) {
        if (_host[0] == '\0') return;                 // belum dikonfigurasi
        if (WiFi.status() != WL_CONNECTED) return;    // tunggu WiFi siap

        uint32_t now = millis();
        uint32_t interval = (uint32_t)_reconnectSec * 1000UL;
        if (!_firstAttempt && (uint32_t)(now - _lastConnectAt) < interval) return;

        _firstAttempt  = false;
        _lastConnectAt = now;
        _attemptConnect();   // blocking singkat — aman, ini konteks pemilik
        return;
    }

    // ── EMQTT_CONNECTED ───────────────────────────────────────────────────────
    if (!_tcp || !_tcp->connected()) {
        _onDisconnected();
        return;
    }

    _drainOutbox();
    _readIncoming();
    if (_state != EMQTT_CONNECTED) return; // _readIncoming mungkin disconnect
    _handleKeepAlive();
    _checkPendingAcks();
}

// ─── Connect (loop context — boleh blocking singkat) ──────────────────────────

bool ElectinsMqtt::_attemptConnect() {
    // Reset outbox & RX state sisa sesi sebelumnya
    _obHead = _obTail = 0;
    _resetRxState();

    if (_client)    { _client->stop();    delete _client;    _client    = nullptr; }
    if (_secureCli) { _secureCli->stop(); delete _secureCli; _secureCli = nullptr; }
    _tcp = nullptr;

    if (_secure) {
        _secureCli = new WiFiClientSecure();
        if (!_secureCli) return false;
        if (_insecure) _secureCli->setInsecure();
        _tcp = _secureCli;
    } else {
        _client = new WiFiClient();
        if (!_client) return false;
        _tcp = _client;
    }

    _tcp->setTimeout(EMQTT_TCP_TIMEOUT / 1000);
    if (!_tcp->connect(_host, _port)) {
        _onDisconnected();
        return false;
    }

    if (!_sendConnect()) {
        _onDisconnected();
        return false;
    }

    // Tunggu CONNACK — blocking dengan yield() (konteks pemilik, aman)
    uint32_t deadline = millis() + EMQTT_CONNACK_WAIT;
    while ((int32_t)(millis() - deadline) < 0) {
        if (_tcp->available() >= 4) {
            uint8_t hdr = _tcp->read();
            if ((hdr & 0xF0) == EMQTT_CONNACK) {
                _tcp->read();             // remaining length (0x02)
                _tcp->read();             // session present flag
                uint8_t rc = _tcp->read();
                if (rc == EMQTT_CONN_ACCEPTED) {
                    _lastPingAt  = millis();
                    _pingPending = false;
                    _resetRxState();
                    memset(_pending, 0, sizeof(_pending));
                    _state = EMQTT_CONNECTED;
                    if (_connectCb) _connectCb();
                    return true;
                }
                _onDisconnected();        // CONNACK ditolak broker
                return false;
            }
        }
        yield();
        delay(1);
    }

    _onDisconnected();                    // timeout CONNACK
    return false;
}

void ElectinsMqtt::disconnect() {
    _wantDisconnect = true;
}

void ElectinsMqtt::_onDisconnected() {
    bool wasConnected = (_state == EMQTT_CONNECTED);

    // Set state DULU agar producer (publish/subscribe di konteks lain)
    // langsung berhenti menyentuh outbox sebelum kita reset indeksnya.
    _state        = EMQTT_DISCONNECTED;
    _pingPending  = false;
    _resetRxState();

    // Reset outbox & pending di bawah mutex (cegah race dengan producer)
    _lock();
    _obHead = _obTail = 0;
    memset(_pending, 0, sizeof(_pending));
    _unlock();

    if (_tcp) { _tcp->stop(); _tcp = nullptr; }
    if (_client)    { delete _client;    _client    = nullptr; }
    if (_secureCli) { delete _secureCli; _secureCli = nullptr; }

    if (wasConnected && _disconnectCb) _disconnectCb();
}

// ─── Outbox (ring buffer) ──────────────────────────────────────────────────────

uint16_t ElectinsMqtt::_obCount() const {
    uint16_t h = _obHead, t = _obTail;
    return (h >= t) ? (uint16_t)(h - t)
                    : (uint16_t)(EMQTT_OUTBOX_SIZE - t + h);
}

uint16_t ElectinsMqtt::_obFree() const {
    // sisakan 1 byte agar head==tail selalu berarti kosong
    return (uint16_t)(EMQTT_OUTBOX_SIZE - 1 - _obCount());
}

// Producer — dipanggil dari publish/subscribe (konteks apa pun). HARUS di-lock.
bool ElectinsMqtt::_enqueue(const uint8_t* data, uint16_t len) {
    if (!data || len == 0) return false;
    if (_obFree() < len) return false; // outbox penuh — paket di-drop
    uint16_t h = _obHead;
    for (uint16_t i = 0; i < len; i++) {
        _outbox[h] = data[i];
        h = (uint16_t)((h + 1) % EMQTT_OUTBOX_SIZE);
    }
    _obHead = h; // publikasikan setelah seluruh byte ditulis
    return true;
}

// Consumer — hanya dipanggil dari loop(). Menulis outbox → socket.
void ElectinsMqtt::_drainOutbox() {
    while (true) {
        _lock();
        uint16_t head = _obHead;
        uint16_t tail = _obTail;
        _unlock();

        if (head == tail) break;            // kosong

        // byte kontigu sampai akhir buffer / sampai head
        uint16_t contig = (head > tail) ? (uint16_t)(head - tail)
                                        : (uint16_t)(EMQTT_OUTBOX_SIZE - tail);

        int space = _tcp->availableForWrite();
        if (space <= 0) space = 256;        // fallback (TLS / nilai tak tersedia)

        uint16_t n = contig;
        if (n > (uint16_t)space) n = (uint16_t)space;
        if (n == 0) break;

        int w = _tcp->write(_outbox + tail, n);
        if (w <= 0) break;                  // socket belum bisa menerima

        _lock();
        _obTail = (uint16_t)((tail + w) % EMQTT_OUTBOX_SIZE);
        _unlock();

        if ((uint16_t)w < n) break;         // partial write — lanjut cycle berikutnya
    }
}

// ─── RX — baca paket masuk, non-blocking, dispatch saat lengkap ───────────────

void ElectinsMqtt::_resetRxState() {
    _rxHeaderDone  = false;
    _rxHdr         = 0;
    _rxRemLenDone  = false;
    _rxRemLen      = 0;
    _rxMultiplier  = 1;
    _rxRemLenBytes = 0;
    _rxPayloadRead = 0;
    _rxOversized   = false;
}

void ElectinsMqtt::_readIncoming() {
    // Proses paket selama datanya tersedia; dispatch segera setelah lengkap
    // (tidak menunggu byte tambahan — perbaikan bug PINGRESP/PUBACK).
    for (uint8_t guard = 0; guard < 32; guard++) {

        // FASE 1: header byte
        if (!_rxHeaderDone) {
            if (_tcp->available() <= 0) return;
            _rxHdr         = (uint8_t)_tcp->read();
            _rxHeaderDone  = true;
            _rxRemLenDone  = false;
            _rxRemLen      = 0;
            _rxMultiplier  = 1;
            _rxRemLenBytes = 0;
            _rxPayloadRead = 0;
            _rxOversized   = false;
        }

        // FASE 2: remaining length (variable byte integer)
        while (!_rxRemLenDone) {
            if (_tcp->available() <= 0) return;
            uint8_t b = (uint8_t)_tcp->read();
            _rxRemLen += (uint32_t)(b & 0x7F) * _rxMultiplier;
            _rxMultiplier <<= 7;
            _rxRemLenBytes++;
            if (!(b & 0x80)) {
                _rxRemLenDone  = true;
                _rxPayloadRead = 0;
                _rxOversized   = (_rxRemLen > EMQTT_RX_BUF_SIZE);
            } else if (_rxRemLenBytes >= 4) {
                _onDisconnected();          // malformed remaining length
                return;
            }
        }

        // FASE 3: payload
        uint32_t remLen = _rxRemLen;

        if (_rxOversized) {
            // Paket terlalu besar — buang byte sampai habis
            while (_rxPayloadRead < remLen && _tcp->available() > 0) {
                _tcp->read();
                _rxPayloadRead++;
            }
            if (_rxPayloadRead < remLen) return; // tunggu sisa byte
            // Beritahu pengguna agar tahu paket di-drop (bukan diam-diam)
            if (_debugCb) {
                char info[48];
                snprintf(info, sizeof(info),
                         "remLen=%u > RX_BUF=%u — dropped",
                         (unsigned)remLen, (unsigned)EMQTT_RX_BUF_SIZE);
                _debugCb("[MQTT] RX OVERSIZED ", info);
            }
            _resetRxState();
            continue;
        }

        if (_rxPayloadRead < remLen) {
            int avail = _tcp->available();
            if (avail > 0) {
                uint32_t need = remLen - _rxPayloadRead;
                uint32_t toRead = ((uint32_t)avail < need) ? (uint32_t)avail : need;
                int got = _tcp->read(_rxBuf + _rxPayloadRead, toRead);
                if (got > 0) _rxPayloadRead += (uint32_t)got;
            }
            if (_rxPayloadRead < remLen) return; // belum lengkap, tunggu
        }

        // Paket lengkap → dispatch
        _handlePacket(_rxHdr, remLen);
        _resetRxState();
        // lanjut loop untuk paket berikutnya bila ada
    }
}

void ElectinsMqtt::_handlePacket(uint8_t hdr, uint32_t remLen) {
    uint8_t type = hdr & 0xF0;
    switch (type) {
        case EMQTT_PUBLISH:
            _dispatchPublish(hdr, remLen);
            break;
        case EMQTT_PUBACK:
            if (remLen >= 2) {
                uint16_t pid = (uint16_t)((_rxBuf[0] << 8) | _rxBuf[1]);
                _clearPending(pid);
                if (_debugCb) {
                    char b[16]; snprintf(b, sizeof(b), "pid=%u", (unsigned)pid);
                    _debugCb("[MQTT] RX PUBACK   ", b);
                }
            }
            break;
        case EMQTT_SUBACK:
            // SUBACK = packet ID (2 byte) + return code per topic.
            // Return code = QoS yang di-grant broker (atau 0x80 = failure).
            if (_debugCb && remLen >= 3) {
                uint16_t pid = (uint16_t)((_rxBuf[0] << 8) | _rxBuf[1]);
                uint8_t  rc  = _rxBuf[2];
                char b[40];
                if (rc == 0x80) {
                    snprintf(b, sizeof(b), "pid=%u FAILURE", (unsigned)pid);
                } else {
                    snprintf(b, sizeof(b), "pid=%u QoS=%u granted",
                             (unsigned)pid, (unsigned)(rc & 0x03));
                }
                _debugCb("[MQTT] RX SUBACK   ", b);
            }
            break;
        case 0x60: // PUBREL — broker lanjutan QoS 2 flow (setelah kita kirim PUBREC)
            if (remLen >= 2) {
                uint16_t pid = (uint16_t)((_rxBuf[0] << 8) | _rxBuf[1]);
                bool ok = _sendPubComp(pid); // selesaikan QoS 2 handshake
                if (_debugCb) _debugCb("[MQTT] TX PUBCOMP  ", ok ? "ok" : "FAILED");
            }
            break;
        case EMQTT_PINGRESP:
            _pingPending = false;
            _lastPingAt  = millis();
            break;
        case EMQTT_UNSUBACK:
        default:
            break; // tidak perlu aksi — payload sudah dikonsumsi ke _rxBuf
    }
}

void ElectinsMqtt::_dispatchPublish(uint8_t hdr, uint32_t remLen) {
    if (remLen < 2) return;

    uint16_t topicLen = (uint16_t)((_rxBuf[0] << 8) | _rxBuf[1]);
    if ((uint32_t)(topicLen + 2) > remLen) return; // malformed

    char topic[EMQTT_MAX_TOPIC_LEN];
    uint16_t tl = (topicLen < sizeof(topic) - 1) ? topicLen : (uint16_t)(sizeof(topic) - 1);
    memcpy(topic, _rxBuf + 2, tl);
    topic[tl] = '\0';

    uint8_t  qos          = (hdr >> 1) & 0x03;
    uint16_t payloadStart = (uint16_t)(2 + topicLen);
    uint16_t pid          = 0;

    if (qos >= 1) {
        if ((uint32_t)(payloadStart + 2) > remLen) return;
        pid = (uint16_t)((_rxBuf[payloadStart] << 8) | _rxBuf[payloadStart + 1]);
        payloadStart += 2;
    }

    if ((uint32_t)payloadStart > remLen) return;
    uint16_t payloadLen = (uint16_t)(remLen - payloadStart);
    bool     retain     = (hdr & 0x01) != 0;

    _rxBuf[remLen] = '\0'; // guard byte — null-terminator aman

    // ── Kirim acknowledgment DULU sebelum panggil callback user ──────────────
    // Penting untuk hardware-sensitive callbacks (IR, NeoPixel, Servo, dll):
    //   - Broker langsung dapat ack → tidak akan retransmit (tidak ada paket
    //     masuk lagi yang men-trigger interrupt jaringan saat callback jalan)
    //   - TCP/WiFi TX queue idle saat callback eksekusi → tidak ada radio
    //     activity yang men-jitter timing pin GPIO
    //
    // QoS 1 → PUBACK
    // QoS 2 → PUBREC (handshake dilanjutkan via case 0x60 PUBREL → PUBCOMP)
    if (qos == 1) {
        bool ok = _sendPubAck(pid);
        if (_debugCb) _debugCb("[MQTT] TX PUBACK   ", ok ? "ok" : "FAILED");
    } else if (qos == 2) {
        bool ok = _sendPubRec(pid);
        if (_debugCb) _debugCb("[MQTT] TX PUBREC   ", ok ? "ok" : "FAILED");
    }

    // Beri kesempatan FreeRTOS scheduler menyelesaikan WiFi TX di core lain
    // sebelum callback user yang mungkin sensitif timing dijalankan.
    if (qos >= 1) yield();

    // Log diagnostik (membantu diagnosa kenapa pesan QoS tertentu tak ter-handle)
    if (_debugCb) {
        char info[64];
        snprintf(info, sizeof(info), "QoS=%u retain=%d len=%u pid=%u",
                 (unsigned)qos, (int)retain,
                 (unsigned)payloadLen, (unsigned)pid);
        _debugCb("[MQTT] RX PUBLISH ", info);
        _debugCb("[MQTT] RX topic    ", topic);
    }

    // ── Sekarang baru panggil callback user — jaringan sudah idle ───────────
    if (_messageCb) {
        _messageCb(topic, (char*)(_rxBuf + payloadStart), payloadLen, qos, retain);
    }
}

// ─── Keepalive ─────────────────────────────────────────────────────────────────

void ElectinsMqtt::_handleKeepAlive() {
    uint32_t now = millis();

    if (_pingPending && (uint32_t)(now - _pingSentAt) > EMQTT_PING_TIMEOUT_MS) {
        _onDisconnected();
        return;
    }

    if (!_pingPending &&
        (uint32_t)(now - _lastPingAt) >= (uint32_t)(_keepAliveSec * 1000UL)) {
        if (_sendPingReq()) {
            _pingPending = true;
            _pingSentAt  = now;
        } else {
            _onDisconnected();
        }
    }
}

// ─── Publish / Subscribe — enqueue ke outbox (thread-safe) ────────────────────

uint16_t ElectinsMqtt::publish(const char* topic, const char* payload,
                                uint16_t payloadLen, bool retain, uint8_t qos) {
    if (_state != EMQTT_CONNECTED || !topic || !payload) return 0;
    qos = qos & 0x01;

    uint16_t topicLen = (uint16_t)strlen(topic);
    uint32_t remLen   = (uint32_t)(2 + topicLen + payloadLen);
    if (qos == 1) remLen += 2;
    if (remLen + 5 > EMQTT_TX_SCRATCH_SIZE) return 0;

    _lock();
    uint16_t pid   = (qos == 1) ? _nextPacketId() : 0;
    uint16_t pos   = 0;
    uint8_t  flags = (retain ? 0x01 : 0x00) | ((qos & 0x01) << 1);

    _txScratch[pos++] = EMQTT_PUBLISH | flags;
    pos = _encodeRemLen(_txScratch, pos, remLen);
    pos = _writeStr(_txScratch, pos, topic);
    if (qos == 1) {
        _txScratch[pos++] = (uint8_t)(pid >> 8);
        _txScratch[pos++] = (uint8_t)(pid & 0xFF);
    }
    memcpy(_txScratch + pos, payload, payloadLen);
    pos += payloadLen;

    bool ok = _enqueue(_txScratch, pos);
    if (ok && qos == 1) _registerPending(pid);
    _unlock();

    if (!ok) return 0;
    return (qos == 1) ? pid : 1;
}

uint16_t ElectinsMqtt::subscribe(const char* topic, uint8_t qos) {
    if (_state != EMQTT_CONNECTED || !topic) return 0;
    if (qos > 2) qos = 2;   // QoS 0, 1, 2 didukung untuk subscribe

    uint16_t topicLen = (uint16_t)strlen(topic);
    uint32_t remLen   = (uint32_t)(2 + 2 + topicLen + 1);
    if (remLen + 5 > EMQTT_TX_SCRATCH_SIZE) return 0;

    _lock();
    uint16_t pid = _nextPacketId();
    uint16_t pos = 0;
    _txScratch[pos++] = EMQTT_SUBSCRIBE;
    pos = _encodeRemLen(_txScratch, pos, remLen);
    _txScratch[pos++] = (uint8_t)(pid >> 8);
    _txScratch[pos++] = (uint8_t)(pid & 0xFF);
    pos = _writeStr(_txScratch, pos, topic);
    _txScratch[pos++] = qos;
    bool ok = _enqueue(_txScratch, pos);
    _unlock();

    return ok ? pid : 0;
}

uint16_t ElectinsMqtt::unsubscribe(const char* topic) {
    if (_state != EMQTT_CONNECTED || !topic) return 0;

    uint16_t topicLen = (uint16_t)strlen(topic);
    uint32_t remLen   = (uint32_t)(2 + 2 + topicLen);
    if (remLen + 5 > EMQTT_TX_SCRATCH_SIZE) return 0;

    _lock();
    uint16_t pid = _nextPacketId();
    uint16_t pos = 0;
    _txScratch[pos++] = EMQTT_UNSUBSCRIBE;
    pos = _encodeRemLen(_txScratch, pos, remLen);
    _txScratch[pos++] = (uint8_t)(pid >> 8);
    _txScratch[pos++] = (uint8_t)(pid & 0xFF);
    pos = _writeStr(_txScratch, pos, topic);
    bool ok = _enqueue(_txScratch, pos);
    _unlock();

    return ok ? pid : 0;
}

// ─── Paket kontrol keluar (langsung dari loop context) ────────────────────────

bool ElectinsMqtt::_writeDirect(const uint8_t* data, uint16_t len) {
    if (!_tcp || !_tcp->connected() || !data || len == 0) return false;
    return _tcp->write(data, len) == len;
}

bool ElectinsMqtt::_sendConnect() {
    const char* clientId = (_clientId[0] != '\0') ? _clientId : "ElectinsIoT";
    bool hasUser = (_user[0] != '\0');
    bool hasPass = (_pass[0] != '\0');

    uint32_t remLen = 10 + 2 + strlen(clientId);
    uint8_t  flags  = 0x02; // clean session

    if (_hasWill) {
        flags |= 0x04 | ((_willQos & 0x01) << 3) | (_willRetain ? 0x20 : 0x00);
        remLen += 2 + strlen(_willTopic) + 2 + strlen(_willPayload);
    }
    if (hasUser) { flags |= 0x80; remLen += 2 + strlen(_user); }
    if (hasPass) { flags |= 0x40; remLen += 2 + strlen(_pass); }

    if (remLen + 5 > EMQTT_TX_SCRATCH_SIZE) return false;

    uint16_t pos = 0;
    _txScratch[pos++] = EMQTT_CONNECT;
    pos = _encodeRemLen(_txScratch, pos, remLen);
    _txScratch[pos++] = 0x00; _txScratch[pos++] = 0x04;
    _txScratch[pos++] = 'M'; _txScratch[pos++] = 'Q';
    _txScratch[pos++] = 'T'; _txScratch[pos++] = 'T';
    _txScratch[pos++] = 0x04; // protocol level 3.1.1
    _txScratch[pos++] = flags;
    _txScratch[pos++] = (uint8_t)(_keepAliveSec >> 8);
    _txScratch[pos++] = (uint8_t)(_keepAliveSec & 0xFF);

    pos = _writeStr(_txScratch, pos, clientId);
    if (_hasWill) {
        pos = _writeStr(_txScratch, pos, _willTopic);
        pos = _writeStr(_txScratch, pos, _willPayload);
    }
    if (hasUser) pos = _writeStr(_txScratch, pos, _user);
    if (hasPass) pos = _writeStr(_txScratch, pos, _pass);

    return _writeDirect(_txScratch, pos);
}

bool ElectinsMqtt::_sendPingReq() {
    uint8_t pkt[2] = { EMQTT_PINGREQ, 0x00 };
    return _writeDirect(pkt, 2);
}

bool ElectinsMqtt::_sendPubAck(uint16_t pid) {
    uint8_t pkt[4] = {
        EMQTT_PUBACK, 0x02,
        (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF)
    };
    return _writeDirect(pkt, 4);
}

bool ElectinsMqtt::_sendPubRec(uint16_t pid) {
    uint8_t pkt[4] = {
        0x50, 0x02, // PUBREC fixed header
        (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF)
    };
    return _writeDirect(pkt, 4);
}

bool ElectinsMqtt::_sendPubComp(uint16_t pid) {
    uint8_t pkt[4] = {
        0x70, 0x02, // PUBCOMP fixed header
        (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF)
    };
    return _writeDirect(pkt, 4);
}

bool ElectinsMqtt::_sendDisconnect() {
    uint8_t pkt[2] = { EMQTT_DISCONNECT, 0x00 };
    return _writeDirect(pkt, 2);
}

// ─── QoS1 pending ACK ──────────────────────────────────────────────────────────
// CATATAN konkurensi:
//   _registerPending() dipanggil dari publish() yang SUDAH memegang _mtx.
//   _clearPending()/_checkPendingAcks() dipanggil dari loop() (tanpa lock),
//   jadi keduanya mengambil _mtx sendiri. Tidak ada nesting → tidak deadlock.

void ElectinsMqtt::_registerPending(uint16_t pid) {
    // Dipanggil di bawah _mtx (oleh publish()) — jangan lock lagi.
    for (uint8_t i = 0; i < EMQTT_PENDING_ACK_MAX; i++) {
        if (!_pending[i].active) {
            _pending[i] = { pid, millis(), true };
            return;
        }
    }
    uint8_t oldest = 0;
    for (uint8_t i = 1; i < EMQTT_PENDING_ACK_MAX; i++) {
        if (_pending[i].sentAt < _pending[oldest].sentAt) oldest = i;
    }
    _pending[oldest] = { pid, millis(), true };
}

void ElectinsMqtt::_clearPending(uint16_t pid) {
    _lock();
    for (uint8_t i = 0; i < EMQTT_PENDING_ACK_MAX; i++) {
        if (_pending[i].active && _pending[i].packetId == pid) {
            _pending[i].active = false;
            break;
        }
    }
    _unlock();
}

void ElectinsMqtt::_checkPendingAcks() {
    uint32_t now = millis();
    _lock();
    for (uint8_t i = 0; i < EMQTT_PENDING_ACK_MAX; i++) {
        if (_pending[i].active &&
            (uint32_t)(now - _pending[i].sentAt) > EMQTT_ACK_EXPIRE_MS) {
            _pending[i].active = false;
        }
    }
    _unlock();
}

// ─── Encode helpers ────────────────────────────────────────────────────────────

uint16_t ElectinsMqtt::_encodeRemLen(uint8_t* buf, uint16_t pos, uint32_t len) {
    do {
        uint8_t b = len & 0x7F;
        len >>= 7;
        if (len > 0) b |= 0x80;
        buf[pos++] = b;
    } while (len > 0);
    return pos;
}

uint16_t ElectinsMqtt::_writeStr(uint8_t* buf, uint16_t pos, const char* str) {
    uint16_t len = (uint16_t)strlen(str);
    buf[pos++] = (uint8_t)(len >> 8);
    buf[pos++] = (uint8_t)(len & 0xFF);
    memcpy(buf + pos, str, len);
    return (uint16_t)(pos + len);
}

uint16_t ElectinsMqtt::_nextPacketId() {
    if (++_packetId == 0) _packetId = 1;
    return _packetId;
}
