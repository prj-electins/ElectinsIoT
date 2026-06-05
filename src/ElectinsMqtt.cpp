/*
 * ElectinsMqtt.cpp — Internal MQTT 3.1.1 Engine
 *
 * Implementasi MQTT 3.1.1 di atas WiFiClient/WiFiClientSecure SDK.
 * poll() 100% non-blocking — aman dipanggil dari Ticker ISR.
 * connect() boleh blocking singkat (maks EMQTT_CONNACK_WAIT ms),
 * dipanggil dari reconnect Ticker yang terpisah dari poll Ticker.
 */

#include "ElectinsMqtt.h"
#include <string.h>
#include <stdio.h>

#define EMQTT_PING_TIMEOUT_MS  10000  // disconnect jika PINGRESP tidak datang
#define EMQTT_ACK_EXPIRE_MS    15000  // hapus pending ACK setelah N ms (clean session)

// ─── Constructor / Destructor ─────────────────────────────────────────────────

ElectinsMqtt::ElectinsMqtt() {
    memset(_pending, 0, sizeof(_pending));
    memset(_txBuf,   0, sizeof(_txBuf));
    memset(_rxBuf,   0, sizeof(_rxBuf));
}

ElectinsMqtt::~ElectinsMqtt() {
    if (_client)    { delete _client;    _client    = nullptr; }
    if (_secureCli) { delete _secureCli; _secureCli = nullptr; }
    _tcp = nullptr;
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

void ElectinsMqtt::setKeepAlive(uint16_t s)    { _keepAliveSec = (s > 0) ? s : 15; }
void ElectinsMqtt::setSecure(bool e)           { _secure   = e; }
void ElectinsMqtt::setInsecure(bool i)         { _insecure = i; }

void ElectinsMqtt::setWill(const char* topic, const char* payload,
                            bool retain, uint8_t qos) {
    if (!topic || !payload) return;
    strncpy(_willTopic,   topic,   sizeof(_willTopic)   - 1);
    strncpy(_willPayload, payload, sizeof(_willPayload) - 1);
    _willTopic[sizeof(_willTopic)-1]   = '\0';
    _willPayload[sizeof(_willPayload)-1] = '\0';
    _willRetain = retain;
    _willQos    = qos & 0x01;
    _hasWill    = true;
}

// ─── Connect ──────────────────────────────────────────────────────────────────
//
// Boleh blocking — dipanggil dari reconnect Ticker (bukan poll Ticker).
// Waktu blok maksimum: EMQTT_CONNACK_WAIT (5 detik) dalam kasus terburuk.

bool ElectinsMqtt::connect() {
    if (_connected) return true;
    if (_host[0] == '\0') return false;

    // Bersihkan state partial read dari sesi sebelumnya
    _hdrReady      = false;
    _remLenStarted = false;
    _remLenDone    = false;
    _remLenAcc     = 0;
    _remLenShift   = 0;
    _readState     = EMQTT_READ_IDLE;
    _partialRead   = 0;

    // Hapus client lama jika ada
    if (_client)    { _client->stop();    delete _client;    _client    = nullptr; }
    if (_secureCli) { _secureCli->stop(); delete _secureCli; _secureCli = nullptr; }
    _tcp = nullptr;

    // Buat client baru
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

    // TCP connect
    _tcp->setTimeout(EMQTT_TCP_TIMEOUT / 1000);
    if (!_tcp->connect(_host, _port)) {
        _onDisconnected();
        return false;
    }

    // Kirim CONNECT
    if (!_sendConnect()) {
        _onDisconnected();
        return false;
    }

    // Tunggu CONNACK — blocking dengan yield() agar WDT tidak reset
    uint32_t deadline = millis() + EMQTT_CONNACK_WAIT;
    while (millis() < deadline) {
        if (_tcp->available() >= 4) {
            uint8_t hdr = _tcp->read();
            if ((hdr & 0xF0) == EMQTT_CONNACK) {
                _tcp->read(); // remaining length (selalu 0x02)
                _tcp->read(); // session present flag
                uint8_t rc = _tcp->read();
                if (rc == EMQTT_CONN_ACCEPTED) {
                    _connected     = true;
                    _lastPingAt    = millis();
                    _pingPending   = false;
                    _hdrReady      = false;
                    _remLenStarted = false;
                    _remLenDone    = false;
                    _readState     = EMQTT_READ_IDLE;
                    _partialRead   = 0;
                    memset(_pending, 0, sizeof(_pending));
                    if (_connectCb) _connectCb();
                    return true;
                }
                _onDisconnected();
                return false;
            }
        }
        yield(); // beri waktu ke WDT dan sistem — tidak delay()
    }

    _onDisconnected();
    return false;
}

bool ElectinsMqtt::connected() {
    if (!_connected) return false;
    if (!_tcp || !_tcp->connected()) {
        _onDisconnected();
        return false;
    }
    return true;
}

void ElectinsMqtt::disconnect() {
    if (_tcp && _tcp->connected()) _sendDisconnect();
    _onDisconnected();
}

// ─── Publish ──────────────────────────────────────────────────────────────────

uint16_t ElectinsMqtt::publish(const char* topic, const char* payload,
                                uint16_t payloadLen, bool retain, uint8_t qos) {
    if (!_connected || !topic || !payload) return 0;
    qos = qos & 0x01;

    uint16_t topicLen = strlen(topic);
    uint32_t remLen   = 2 + topicLen + payloadLen;
    if (qos == 1) remLen += 2;
    if (remLen + 5 > EMQTT_TX_BUF_SIZE) return 0;

    uint16_t pid = (qos == 1) ? _nextPacketId() : 0;
    uint16_t pos = 0;
    uint8_t  flags = (retain ? 0x01 : 0x00) | ((qos & 0x01) << 1);

    _txBuf[pos++] = EMQTT_PUBLISH | flags;
    pos = _encodeRemLen(_txBuf, pos, remLen);
    pos = _writeStr(_txBuf, pos, topic);

    if (qos == 1) {
        _txBuf[pos++] = pid >> 8;
        _txBuf[pos++] = pid & 0xFF;
    }

    memcpy(_txBuf + pos, payload, payloadLen);
    pos += payloadLen;

    if (!_sendBuf(_txBuf, pos)) {
        _onDisconnected();
        return 0;
    }

    if (qos == 1) _registerPending(pid);
    return (qos == 1) ? pid : 1;
}

// ─── Subscribe / Unsubscribe ──────────────────────────────────────────────────

uint16_t ElectinsMqtt::subscribe(const char* topic, uint8_t qos) {
    if (!_connected || !topic) return 0;
    qos = qos & 0x01;

    uint16_t topicLen = strlen(topic);
    uint32_t remLen   = 2 + 2 + topicLen + 1;
    if (remLen + 5 > EMQTT_TX_BUF_SIZE) return 0;

    uint16_t pid = _nextPacketId();
    uint16_t pos = 0;
    _txBuf[pos++] = EMQTT_SUBSCRIBE;
    pos = _encodeRemLen(_txBuf, pos, remLen);
    _txBuf[pos++] = pid >> 8;
    _txBuf[pos++] = pid & 0xFF;
    pos = _writeStr(_txBuf, pos, topic);
    _txBuf[pos++] = qos;

    if (!_sendBuf(_txBuf, pos)) { _onDisconnected(); return 0; }
    return pid;
}

uint16_t ElectinsMqtt::unsubscribe(const char* topic) {
    if (!_connected || !topic) return 0;

    uint16_t topicLen = strlen(topic);
    uint32_t remLen   = 2 + 2 + topicLen;
    if (remLen + 5 > EMQTT_TX_BUF_SIZE) return 0;

    uint16_t pid = _nextPacketId();
    uint16_t pos = 0;
    _txBuf[pos++] = EMQTT_UNSUBSCRIBE;
    pos = _encodeRemLen(_txBuf, pos, remLen);
    _txBuf[pos++] = pid >> 8;
    _txBuf[pos++] = pid & 0xFF;
    pos = _writeStr(_txBuf, pos, topic);

    if (!_sendBuf(_txBuf, pos)) { _onDisconnected(); return 0; }
    return pid;
}

// ─── POLL — 100% non-blocking, aman dari Ticker ISR ──────────────────────────
//
// State machine multi-cycle:
//   Cycle N   : baca header byte (jika tersedia)
//   Cycle N+? : baca remaining length byte per byte (non-blocking)
//   Cycle N+? : kumpulkan payload ke _rxBuf byte per byte (non-blocking)
//   Saat penuh: dispatch paket
//
void ElectinsMqtt::poll() {
    if (!_connected) return;

    if (!_tcp || !_tcp->connected()) {
        _onDisconnected();
        return;
    }

    uint32_t now = millis();

    // ── Cek PINGRESP timeout ─────────────────────────────────────────────────
    if (_pingPending && (now - _pingSentAt) > EMQTT_PING_TIMEOUT_MS) {
        _onDisconnected();
        return;
    }

    // ── Keepalive ping ────────────────────────────────────────────────────────
    if (!_pingPending &&
        (uint32_t)(now - _lastPingAt) >= (uint32_t)(_keepAliveSec * 1000UL)) {
        if (_sendPingReq()) {
            _pingPending = true;
            _pingSentAt  = now;
        } else {
            _onDisconnected();
            return;
        }
    }

    // ── Expire pending ACK lama ───────────────────────────────────────────────
    _checkPendingAcks();

    // ── Baca paket masuk — non-blocking state machine ─────────────────────────
    // Batasi iterasi agar tidak monopoli CPU terlalu lama per cycle
    for (uint8_t iter = 0; iter < 16 && _tcp->available() > 0; iter++) {

        // FASE 1: baca header byte
        if (!_hdrReady) {
            _hdrByte  = _tcp->read();
            _hdrReady = true;
            // Reset state remaining length
            _remLenStarted = false;
            _remLenDone    = false;
            _remLenAcc     = 0;
            _remLenShift   = 0;
            continue; // lanjut ke cycle berikutnya untuk baca remLen
        }

        // FASE 2: decode remaining length, byte per byte, non-blocking
        if (!_remLenDone) {
            if (!_tcp->available()) break; // data belum ada, tunggu cycle berikutnya
            uint8_t b = _tcp->read();
            _remLenAcc |= (uint32_t)(b & 0x7F) << _remLenShift;
            _remLenShift += 7;
            if (!(b & 0x80)) {
                _remLenDone    = true;
                _remLenStarted = true;
                // Siapkan state baca payload
                _readState   = EMQTT_READ_IDLE;
                _partialRead = 0;
            } else if (_remLenShift >= 28) {
                // > 4 byte — malformed packet, reset koneksi
                _onDisconnected();
                return;
            }
            continue;
        }

        // FASE 3: proses paket berdasarkan tipe
        uint8_t  type   = _hdrByte & 0xF0;
        uint32_t remLen = _remLenAcc;

        if (type == EMQTT_PUBLISH) {
            // PUBLISH: kumpulkan payload dulu sebelum dispatch
            if (remLen == 0 || remLen > EMQTT_RX_BUF_SIZE) {
                // Drain paket oversized/empty, byte per cycle
                uint32_t toDrain = remLen - _partialRead;
                uint32_t canRead = _tcp->available();
                uint32_t drain   = (toDrain < canRead) ? toDrain : canRead;
                _partialRead += drain;
                while (drain-- > 0) _tcp->read();
                if (_partialRead >= remLen) {
                    _hdrReady = false; // paket selesai, siap untuk berikutnya
                }
                break;
            }

            // Baca byte payload yang sudah tersedia
            uint32_t canRead = _tcp->available();
            uint32_t needed  = remLen - _partialRead;
            uint32_t toRead  = (needed < canRead) ? needed : canRead;
            // Baca maksimum tersedia tapi jangan lebih dari buffer
            if (toRead > 0) {
                _tcp->readBytes((char*)(_rxBuf + _partialRead), toRead);
                _partialRead += toRead;
            }

            if (_partialRead >= remLen) {
                // Payload lengkap — dispatch
                _dispatchPublish();
                _hdrReady    = false;
                _partialRead = 0;
            }
            break; // satu PUBLISH per cycle poll agar tidak blok terlalu lama

        } else {
            // Paket non-PUBLISH: baca seluruhnya jika sudah cukup tersedia
            uint32_t avail = _tcp->available();
            if (avail < remLen) break; // data belum lengkap, tunggu

            switch (type) {
                case EMQTT_PUBACK:   _handlePubAck();           break;
                case EMQTT_SUBACK:   _handleSubAck(remLen);     break;
                case EMQTT_PINGRESP: _handlePingResp();         break;
                default:
                    // Drain paket tidak dikenal
                    for (uint32_t i = 0; i < remLen; i++) _tcp->read();
                    break;
            }
            _hdrReady = false; // siap baca paket berikutnya
        }
    }
}

// ─── Dispatch PUBLISH ─────────────────────────────────────────────────────────
// Payload sudah lengkap di _rxBuf[0.._partialRead-1]

void ElectinsMqtt::_dispatchPublish() {
    uint32_t remLen = _partialRead;
    if (remLen < 2) return;

    // Parse topic length
    uint16_t topicLen = (_rxBuf[0] << 8) | _rxBuf[1];
    if ((uint32_t)(topicLen + 2) > remLen) return; // malformed

    char topic[EMQTT_MAX_TOPIC_LEN];
    uint16_t tl = (topicLen < sizeof(topic) - 1) ? topicLen : (uint16_t)(sizeof(topic) - 1);
    memcpy(topic, _rxBuf + 2, tl);
    topic[tl] = '\0';

    uint8_t  qos          = (_hdrByte >> 1) & 0x03;
    uint16_t payloadStart = 2 + topicLen;
    uint16_t pid          = 0;

    if (qos >= 1) {
        if ((uint32_t)(payloadStart + 2) > remLen) return;
        pid = (_rxBuf[payloadStart] << 8) | _rxBuf[payloadStart + 1];
        payloadStart += 2;
    }

    if ((uint32_t)payloadStart > remLen) return;
    uint16_t payloadLen = (uint16_t)(remLen - payloadStart);
    bool     retain     = (_hdrByte & 0x01) != 0;

    // Null-terminate aman: _rxBuf punya ukuran EMQTT_RX_GUARD_SIZE = RX_BUF_SIZE+1
    // remLen <= RX_BUF_SIZE, jadi _rxBuf[remLen] selalu valid (indeks guard)
    _rxBuf[remLen] = '\0';

    if (_messageCb) {
        _messageCb(topic, (char*)(_rxBuf + payloadStart), payloadLen, qos, retain);
    }

    if (qos == 1) _sendPubAck(pid);
}

// ─── Paket masuk ─────────────────────────────────────────────────────────────

void ElectinsMqtt::_handlePubAck() {
    // Baca 2 byte packet ID
    if (_tcp->available() < 2) return;
    uint8_t hi = _tcp->read();
    uint8_t lo = _tcp->read();
    _clearPending((hi << 8) | lo);
}

void ElectinsMqtt::_handleSubAck(uint32_t remLen) {
    for (uint32_t i = 0; i < remLen; i++) {
        if (_tcp->available()) _tcp->read();
    }
}

void ElectinsMqtt::_handlePingResp() {
    _pingPending = false;
    _lastPingAt  = millis();
}

// ─── Paket keluar ─────────────────────────────────────────────────────────────

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

    if (remLen + 5 > EMQTT_TX_BUF_SIZE) return false;

    uint16_t pos = 0;
    _txBuf[pos++] = EMQTT_CONNECT;
    pos = _encodeRemLen(_txBuf, pos, remLen);
    _txBuf[pos++] = 0x00; _txBuf[pos++] = 0x04;
    _txBuf[pos++] = 'M'; _txBuf[pos++] = 'Q';
    _txBuf[pos++] = 'T'; _txBuf[pos++] = 'T';
    _txBuf[pos++] = 0x04; // protocol 3.1.1
    _txBuf[pos++] = flags;
    _txBuf[pos++] = _keepAliveSec >> 8;
    _txBuf[pos++] = _keepAliveSec & 0xFF;

    pos = _writeStr(_txBuf, pos, clientId);
    if (_hasWill) {
        pos = _writeStr(_txBuf, pos, _willTopic);
        pos = _writeStr(_txBuf, pos, _willPayload);
    }
    if (hasUser) pos = _writeStr(_txBuf, pos, _user);
    if (hasPass) pos = _writeStr(_txBuf, pos, _pass);

    return _sendBuf(_txBuf, pos);
}

bool ElectinsMqtt::_sendPingReq() {
    uint8_t pkt[2] = { EMQTT_PINGREQ, 0x00 };
    return _sendBuf(pkt, 2);
}

bool ElectinsMqtt::_sendPubAck(uint16_t pid) {
    uint8_t pkt[4] = {
        EMQTT_PUBACK, 0x02,
        (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF)
    };
    return _sendBuf(pkt, 4);
}

bool ElectinsMqtt::_sendDisconnect() {
    uint8_t pkt[2] = { EMQTT_DISCONNECT, 0x00 };
    return _sendBuf(pkt, 2);
}

// ─── QoS1 pending ACK ────────────────────────────────────────────────────────

void ElectinsMqtt::_registerPending(uint16_t pid) {
    for (uint8_t i = 0; i < EMQTT_PENDING_ACK_MAX; i++) {
        if (!_pending[i].active) {
            _pending[i] = { pid, millis(), true };
            return;
        }
    }
    // Semua slot penuh — overwrite yang paling lama
    uint8_t oldest = 0;
    for (uint8_t i = 1; i < EMQTT_PENDING_ACK_MAX; i++) {
        if (_pending[i].sentAt < _pending[oldest].sentAt) oldest = i;
    }
    _pending[oldest] = { pid, millis(), true };
}

void ElectinsMqtt::_clearPending(uint16_t pid) {
    for (uint8_t i = 0; i < EMQTT_PENDING_ACK_MAX; i++) {
        if (_pending[i].active && _pending[i].packetId == pid) {
            _pending[i].active = false;
            return;
        }
    }
}

void ElectinsMqtt::_checkPendingAcks() {
    // Dengan clean session, tidak perlu retransmit setelah reconnect.
    // Cukup hapus entry yang sudah sangat lama agar slot tidak penuh.
    uint32_t now = millis();
    for (uint8_t i = 0; i < EMQTT_PENDING_ACK_MAX; i++) {
        if (_pending[i].active &&
            (now - _pending[i].sentAt) > EMQTT_ACK_EXPIRE_MS) {
            _pending[i].active = false;
        }
    }
}

// ─── Encode helpers ───────────────────────────────────────────────────────────

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
    uint16_t len = strlen(str);
    buf[pos++] = len >> 8;
    buf[pos++] = len & 0xFF;
    memcpy(buf + pos, str, len);
    return pos + len;
}

bool ElectinsMqtt::_sendBuf(const uint8_t* data, uint16_t len) {
    if (!_tcp || !_tcp->connected() || !data || len == 0) return false;
    return _tcp->write(data, len) == len;
}

uint16_t ElectinsMqtt::_nextPacketId() {
    if (++_packetId == 0) _packetId = 1;
    return _packetId;
}

// ─── Disconnect internal ──────────────────────────────────────────────────────

void ElectinsMqtt::_onDisconnected() {
    bool wasConnected = _connected;
    _connected     = false;
    _pingPending   = false;
    _hdrReady      = false;
    _remLenStarted = false;
    _remLenDone    = false;
    _readState     = EMQTT_READ_IDLE;
    _partialRead   = 0;
    memset(_pending, 0, sizeof(_pending));

    // Tutup TCP lalu hapus objek
    if (_tcp) { _tcp->stop(); _tcp = nullptr; }
    if (_client)    { delete _client;    _client    = nullptr; }
    if (_secureCli) { delete _secureCli; _secureCli = nullptr; }

    // Panggil callback hanya jika sebelumnya memang connected
    // (cegah double-call jika _onDisconnected dipanggil dua kali)
    if (wasConnected && _disconnectCb) _disconnectCb();
}
