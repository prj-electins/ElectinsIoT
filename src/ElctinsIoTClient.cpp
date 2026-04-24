#include "ElctinsIoTClient.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ─── Constructor / Destructor ─────────────────────────────────────────────────

ElctinsIoTClient::ElctinsIoTClient(Client& client) : _client(client) {
    _buf = (uint8_t*)malloc(MQTT_BUFFER_SIZE);
    if (!_buf) _bufferSize = 0;
    memset(_subs, 0, sizeof(_subs));
}

ElctinsIoTClient::~ElctinsIoTClient() {
    if (_buf) free(_buf);
}

// ─── Config ───────────────────────────────────────────────────────────────────

void ElctinsIoTClient::setServer(const char* host, uint16_t port) {
    if (!host) return;
    _host = host; _port = port;
}

void ElctinsIoTClient::setCredentials(const char* user, const char* pass) {
    _user = user; _pass = pass;
}

void ElctinsIoTClient::setClientId(const char* clientId) { _clientId = clientId; }
void ElctinsIoTClient::setKeepAlive(uint16_t seconds)    { _keepAlive = seconds > 0 ? seconds : 1; }
void ElctinsIoTClient::setDebug(bool enable)             { _debug = enable; }

void ElctinsIoTClient::setWill(const char* topic, const char* payload, bool retain, MqttQoS qos) {
    if (!topic || !payload) return;
    _willTopic = topic; _willPayload = payload;
    _willRetain = retain; _willQos = qos;
}

void ElctinsIoTClient::setBufferSize(uint16_t size) {
    if (size < 64) return; // minimum sensible buffer
    if (_buf) free(_buf);
    _buf = (uint8_t*)malloc(size);
    _bufferSize = _buf ? size : 0;
}

void ElctinsIoTClient::enableReconnect(bool enable, uint32_t intervalMs) {
    _reconnect = enable;
    _reconnectInterval = intervalMs < 1000 ? 1000 : intervalMs; // minimum 1 detik
}

// ─── Callbacks ────────────────────────────────────────────────────────────────

void ElctinsIoTClient::onConnect(MqttConnectCallback cb)       { _connectCb    = cb; }
void ElctinsIoTClient::onDisconnect(MqttDisconnectCallback cb) { _disconnectCb = cb; }
void ElctinsIoTClient::onMessage(MqttCallback cb)              { _globalCb     = cb; }

// ─── One-line begin ───────────────────────────────────────────────────────────

bool ElctinsIoTClient::begin(const char* ssid, const char* wifiPass,
                              const char* host, uint16_t port,
                              const char* clientId,
                              const char* user, const char* mqttPass) {
    if (!ssid || !host || !clientId) return false;

    WiFi.begin(ssid, wifiPass ? wifiPass : "");
    Serial.print("[WiFi] Connecting");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 30000) { Serial.println(" Timeout!"); return false; }
        delay(500); Serial.print(".");
    }
    Serial.println(" OK");

    setServer(host, port);
    if (user) setCredentials(user, mqttPass);
    enableReconnect(true, 5000);
    return connect(clientId);
}

// ─── Connect ──────────────────────────────────────────────────────────────────

bool ElctinsIoTClient::connect() {
    if (_connected) return true;
    if (!_host)     { _log("[MQTT] No server set"); return false; }
    return _doConnect();
}

bool ElctinsIoTClient::connect(const char* clientId) {
    if (clientId) _clientId = clientId;
    return connect();
}

bool ElctinsIoTClient::connect(const char* clientId, const char* user, const char* pass) {
    if (clientId) _clientId = clientId;
    _user = user; _pass = pass;
    return connect();
}

void ElctinsIoTClient::disconnect() {
    if (!_connected) return;
    uint8_t pkt[2] = { MQTT_DISCONNECT, 0x00 };
    _sendPacket(pkt, 2);
    _client.stop();
    _connected = false;
    _log("[MQTT] Disconnected");
    if (_disconnectCb) _disconnectCb();
}

bool ElctinsIoTClient::connected() {
    if (_connected && !_client.connected()) {
        _connected = false;
        _log("[MQTT] Connection lost");
        if (_disconnectCb) _disconnectCb();
    }
    return _connected;
}

// ─── Publish ──────────────────────────────────────────────────────────────────

bool ElctinsIoTClient::publish(const char* topic, const char* payload, bool retain, MqttQoS qos) {
    if (!topic || !payload) return false;
    return publish(topic, (const uint8_t*)payload, (uint16_t)strlen(payload), retain, qos);
}

bool ElctinsIoTClient::publish(const char* topic, int value, bool retain) {
    char buf[16]; snprintf(buf, sizeof(buf), "%d", value);
    return publish(topic, buf, retain);
}

bool ElctinsIoTClient::publish(const char* topic, float value, uint8_t decimals, bool retain) {
    char buf[24]; dtostrf(value, 1, decimals, buf);
    return publish(topic, buf, retain);
}

bool ElctinsIoTClient::publish(const char* topic, bool value, bool retain) {
    return publish(topic, value ? "true" : "false", retain);
}

bool ElctinsIoTClient::publish(const char* topic, const uint8_t* payload, uint16_t length, bool retain, MqttQoS qos) {
    if (!_connected || !_buf || !topic || !payload) return false;

    uint16_t topicLen  = strlen(topic);
    uint32_t remaining = 2 + topicLen + length;
    if (qos > QOS0) remaining += 2;

    // Guard: packet harus muat di buffer (header 1 byte + remaining length max 4 bytes)
    if (remaining + 5 > _bufferSize) {
        _log("[MQTT] Publish: payload too large");
        return false;
    }

    uint16_t pos = 0;
    _buf[pos++] = MQTT_PUBLISH | (retain ? 0x01 : 0x00) | ((qos & 0x03) << 1);
    pos = _encodeLength(_buf, pos, remaining);
    pos = _writeString(_buf, pos, topic);

    uint16_t pid = 0;
    if (qos > QOS0) {
        pid = _nextPacketId();
        _buf[pos++] = pid >> 8;
        _buf[pos++] = pid & 0xFF;
    }

    memcpy(_buf + pos, payload, length);
    pos += length;

    if (!_sendPacket(_buf, pos)) return false;
    _log("[MQTT] Publish: ", topic);

    if (qos == QOS1) return _waitFor(MQTT_PUBACK);
    if (qos == QOS2) {
        if (!_waitFor(MQTT_PUBREC)) return false;
        uint8_t pubrel[4] = { MQTT_PUBREL, 0x02, (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF) };
        _sendPacket(pubrel, 4);
        return _waitFor(MQTT_PUBCOMP);
    }
    return true;
}

ElctinsIoTClient& ElctinsIoTClient::operator<<(const char* topicPayload) {
    if (!topicPayload) return *this;
    char tmp[256];
    strncpy(tmp, topicPayload, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char* sep = strchr(tmp, ':');
    if (sep) { *sep = '\0'; publish(tmp, sep + 1); }
    return *this;
}

// ─── Subscribe ────────────────────────────────────────────────────────────────

bool ElctinsIoTClient::_sendSubscribe(const char* topic, uint8_t qos) {
    if (!_connected || !_buf || !topic) return false;
    uint16_t topicLen  = strlen(topic);
    uint32_t remaining = 2 + 2 + topicLen + 1;
    if (remaining + 5 > _bufferSize) return false;

    uint16_t pos = 0;
    _buf[pos++] = MQTT_SUBSCRIBE;
    pos = _encodeLength(_buf, pos, remaining);
    uint16_t pid = _nextPacketId();
    _buf[pos++] = pid >> 8; _buf[pos++] = pid & 0xFF;
    pos = _writeString(_buf, pos, topic);
    _buf[pos++] = qos & 0x03;
    _log("[MQTT] Subscribe: ", topic);
    return _sendPacket(_buf, pos);
}

bool ElctinsIoTClient::subscribe(const char* topic, MqttQoS qos) {
    if (!topic) return false;
    for (uint8_t i = 0; i < _subCount; i++) {
        if (strcmp(_subs[i].topic, topic) == 0) {
            _subs[i].qos = qos; _subs[i].active = true;
            return _sendSubscribe(topic, qos);
        }
    }
    if (_subCount < MQTT_MAX_SUBS) {
        strncpy(_subs[_subCount].topic, topic, sizeof(_subs[0].topic) - 1);
        _subs[_subCount].topic[sizeof(_subs[0].topic) - 1] = '\0';
        _subs[_subCount].callback      = nullptr;
        _subs[_subCount].paramCallback = nullptr;
        _subs[_subCount].qos           = qos;
        _subs[_subCount].active        = true;
        _subCount++;
    }
    return _sendSubscribe(topic, qos);
}

bool ElctinsIoTClient::subscribe(const char* topic, MqttParamCallback cb, MqttQoS qos) {
    if (!topic) return false;
    for (uint8_t i = 0; i < _subCount; i++) {
        if (strcmp(_subs[i].topic, topic) == 0) {
            _subs[i].paramCallback = cb; _subs[i].callback = nullptr;
            _subs[i].qos = qos; _subs[i].active = true;
            return _sendSubscribe(topic, qos);
        }
    }
    if (_subCount < MQTT_MAX_SUBS) {
        strncpy(_subs[_subCount].topic, topic, sizeof(_subs[0].topic) - 1);
        _subs[_subCount].topic[sizeof(_subs[0].topic) - 1] = '\0';
        _subs[_subCount].callback      = nullptr;
        _subs[_subCount].paramCallback = cb;
        _subs[_subCount].qos           = qos;
        _subs[_subCount].active        = true;
        _subCount++;
    }
    return _sendSubscribe(topic, qos);
}

bool ElctinsIoTClient::subscribe(const char* topic, MqttTopicCallback cb, MqttQoS qos) {
    if (!topic) return false;
    for (uint8_t i = 0; i < _subCount; i++) {
        if (strcmp(_subs[i].topic, topic) == 0) {
            _subs[i].callback = cb; _subs[i].paramCallback = nullptr;
            _subs[i].qos = qos; _subs[i].active = true;
            return _sendSubscribe(topic, qos);
        }
    }
    if (_subCount < MQTT_MAX_SUBS) {
        strncpy(_subs[_subCount].topic, topic, sizeof(_subs[0].topic) - 1);
        _subs[_subCount].topic[sizeof(_subs[0].topic) - 1] = '\0';
        _subs[_subCount].callback      = cb;
        _subs[_subCount].paramCallback = nullptr;
        _subs[_subCount].qos           = qos;
        _subs[_subCount].active        = true;
        _subCount++;
    }
    return _sendSubscribe(topic, qos);
}

bool ElctinsIoTClient::unsubscribe(const char* topic) {
    if (!topic) return false;
    for (uint8_t i = 0; i < _subCount; i++)
        if (strcmp(_subs[i].topic, topic) == 0) _subs[i].active = false;

    if (!_connected || !_buf) return false;
    uint16_t topicLen  = strlen(topic);
    uint32_t remaining = 2 + 2 + topicLen;
    if (remaining + 5 > _bufferSize) return false;

    uint16_t pos = 0;
    _buf[pos++] = MQTT_UNSUBSCRIBE;
    pos = _encodeLength(_buf, pos, remaining);
    uint16_t pid = _nextPacketId();
    _buf[pos++] = pid >> 8; _buf[pos++] = pid & 0xFF;
    pos = _writeString(_buf, pos, topic);
    _log("[MQTT] Unsubscribe: ", topic);
    return _sendPacket(_buf, pos);
}

// ─── Run / Loop ───────────────────────────────────────────────────────────────

void ElctinsIoTClient::run() { loop(); }

void ElctinsIoTClient::loop() {
    uint32_t now = millis();
    if (!connected()) {
        if (_reconnect && (now - _lastReconnectAttempt >= _reconnectInterval)) {
            _lastReconnectAttempt = now;
            _log("[MQTT] Reconnecting...");
            _doConnect();
        }
        return;
    }
    // Keepalive ping
    if ((uint32_t)(now - _lastPing) >= (uint32_t)(_keepAlive * 1000UL)) {
        uint8_t ping[2] = { MQTT_PINGREQ, 0x00 };
        _sendPacket(ping, 2);
        _lastPing = now;
        _log("[MQTT] Ping");
    }
    // Proses semua data yang tersedia
    while (_client.available()) _processIncoming();
}

// ─── Internals ────────────────────────────────────────────────────────────────

bool ElctinsIoTClient::_doConnect() {
    if (!_buf)  { _log("[MQTT] No buffer");    return false; }
    if (!_host) { _log("[MQTT] No host set");  return false; }

    if (!_client.connect(_host, _port)) { _log("[MQTT] TCP failed"); return false; }

    const char* clientId = (_clientId && strlen(_clientId) > 0) ? _clientId : "ElctinsIoT";

    uint8_t flags = 0x02; // clean session
    if (_user && strlen(_user) > 0) flags |= 0x80;
    if (_pass && strlen(_pass) > 0) flags |= 0x40;
    if (_willTopic)                 flags |= 0x04 | ((_willQos & 0x03) << 3) | (_willRetain ? 0x20 : 0x00);

    uint32_t remaining = 10 + 2 + strlen(clientId);
    if (_willTopic)                 remaining += 2 + strlen(_willTopic) + 2 + strlen(_willPayload);
    if (_user && strlen(_user) > 0) remaining += 2 + strlen(_user);
    if (_pass && strlen(_pass) > 0) remaining += 2 + strlen(_pass);

    // Guard: CONNECT packet harus muat di buffer
    if (remaining + 5 > _bufferSize) {
        _client.stop();
        _log("[MQTT] CONNECT packet too large");
        return false;
    }

    uint16_t pos = 0;
    _buf[pos++] = MQTT_CONNECT;
    pos = _encodeLength(_buf, pos, remaining);
    _buf[pos++] = 0x00; _buf[pos++] = 0x04;
    _buf[pos++] = 'M'; _buf[pos++] = 'Q'; _buf[pos++] = 'T'; _buf[pos++] = 'T';
    _buf[pos++] = 0x04; // protocol level 3.1.1
    _buf[pos++] = flags;
    _buf[pos++] = _keepAlive >> 8; _buf[pos++] = _keepAlive & 0xFF;
    pos = _writeString(_buf, pos, clientId);
    if (_willTopic) {
        pos = _writeString(_buf, pos, _willTopic);
        pos = _writeString(_buf, pos, _willPayload);
    }
    if (_user && strlen(_user) > 0) pos = _writeString(_buf, pos, _user);
    if (_pass && strlen(_pass) > 0) pos = _writeString(_buf, pos, _pass);

    if (!_sendPacket(_buf, pos)) { _client.stop(); return false; }
    if (!_waitFor(MQTT_CONNACK)) { _client.stop(); return false; }

    _connected = true;
    _lastPing  = millis();
    _log("[MQTT] Connected");
    _resubscribeAll();
    if (_connectCb) _connectCb();
    return true;
}

void ElctinsIoTClient::_resubscribeAll() {
    for (uint8_t i = 0; i < _subCount; i++) {
        if (_subs[i].active) _sendSubscribe(_subs[i].topic, _subs[i].qos);
    }
}

bool ElctinsIoTClient::_sendPacket(uint8_t* data, uint16_t len) {
    if (!data || len == 0) return false;
    return _client.write(data, len) == len;
}

bool ElctinsIoTClient::_waitFor(uint8_t type, uint32_t timeout) {
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < timeout) {
        if (_client.available() >= 2) {
            uint8_t hdr = _client.read();
            // Drain remaining length (variable length encoding)
            uint8_t b;
            do {
                if (!_client.available()) return false;
                b = _client.read();
            } while (b & 0x80);
            if ((hdr & 0xF0) == (type & 0xF0) || hdr == type) return true;
            return false;
        }
        yield();
    }
    return false;
}

void ElctinsIoTClient::_processIncoming() {
    if (!_client.available() || !_buf) return;
    uint8_t hdr = _client.read();

    // Decode remaining length (variable length encoding, max 4 bytes)
    uint32_t remaining = 0, shift = 0;
    uint8_t b;
    uint8_t lenBytes = 0;
    do {
        if (!_client.available() || lenBytes >= 4) return; // malformed packet guard
        b = _client.read(); lenBytes++;
        remaining |= (uint32_t)(b & 0x7F) << shift;
        shift += 7;
    } while (b & 0x80);

    if ((hdr & 0xF0) == MQTT_PUBLISH) {
        // Discard oversized packets
        if (remaining > _bufferSize) {
            for (uint32_t i = 0; i < remaining; i++) {
                uint32_t wait = millis();
                while (!_client.available()) { if (millis() - wait > 2000) return; yield(); }
                _client.read();
            }
            return;
        }

        // Read full packet into buffer
        uint32_t rd = 0;
        while (rd < remaining) {
            uint32_t wait = millis();
            while (!_client.available()) { if (millis() - wait > 2000) return; yield(); }
            _buf[rd++] = _client.read();
        }

        // Validate topic length
        if (remaining < 2) return;
        uint16_t topicLen = (_buf[0] << 8) | _buf[1];
        if (topicLen + 2 > remaining) return; // malformed

        char topic[128];
        uint16_t tl = topicLen < (uint16_t)(sizeof(topic) - 1) ? topicLen : (uint16_t)(sizeof(topic) - 1);
        memcpy(topic, _buf + 2, tl);
        topic[tl] = '\0';

        uint8_t  qos          = (hdr >> 1) & 0x03;
        uint16_t payloadStart = 2 + topicLen;
        uint16_t pid          = 0;

        if (qos > 0) {
            if (payloadStart + 2 > remaining) return; // malformed
            pid = (_buf[payloadStart] << 8) | _buf[payloadStart + 1];
            payloadStart += 2;
        }

        if (payloadStart > remaining) return; // malformed
        uint16_t payloadLen = remaining - payloadStart;

        _log("[MQTT] Recv: ", topic);
        _dispatchMessage(topic, _buf + payloadStart, payloadLen);

        if (qos == 1) {
            uint8_t puback[4] = { MQTT_PUBACK, 0x02, (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF) };
            _sendPacket(puback, 4);
        } else if (qos == 2) {
            uint8_t pubrec[4] = { MQTT_PUBREC, 0x02, (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF) };
            _sendPacket(pubrec, 4);
            if (_waitFor(MQTT_PUBREL)) {
                uint8_t pubcomp[4] = { MQTT_PUBCOMP, 0x02, (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF) };
                _sendPacket(pubcomp, 4);
            }
        }

    } else if (hdr == MQTT_PINGRESP) {
        _log("[MQTT] Pong");
    } else {
        // Drain unknown/unhandled packet dengan timeout guard
        for (uint32_t i = 0; i < remaining; i++) {
            uint32_t wait = millis();
            while (!_client.available()) { if (millis() - wait > 2000) return; yield(); }
            _client.read();
        }
    }
}

void ElctinsIoTClient::_dispatchMessage(const char* topic, const uint8_t* payload, uint16_t length) {
    for (uint8_t i = 0; i < _subCount; i++) {
        if (!_subs[i].active || !_topicMatches(_subs[i].topic, topic)) continue;
        if (_subs[i].paramCallback) {
            MqttParam param(payload, length);
            _subs[i].paramCallback(param);
        } else if (_subs[i].callback) {
            _subs[i].callback(payload, length);
        }
    }
    // Global fallback selalu dipanggil
    if (_globalCb) _globalCb(topic, payload, length);
}

bool ElctinsIoTClient::_topicMatches(const char* filter, const char* topic) {
    if (!filter || !topic) return false;
    const char* f = filter;
    const char* t = topic;
    while (*f && *t) {
        if (*f == '#') return true;          // multi-level wildcard: match sisa apapun
        if (*f == '+') {                     // single-level wildcard: skip satu level
            while (*t && *t != '/') t++;
            f++;
            continue;
        }
        if (*f != *t) return false;
        f++; t++;
    }
    if (*f == '#') return true;              // filter berakhir dengan '#'
    return (*f == '\0' && *t == '\0');
}

uint16_t ElctinsIoTClient::_writeString(uint8_t* buf, uint16_t pos, const char* str) {
    uint16_t len = strlen(str);
    buf[pos++] = len >> 8;
    buf[pos++] = len & 0xFF;
    memcpy(buf + pos, str, len);
    return pos + len;
}

uint16_t ElctinsIoTClient::_encodeLength(uint8_t* buf, uint16_t pos, uint32_t len) {
    do {
        uint8_t b = len & 0x7F;
        len >>= 7;
        if (len > 0) b |= 0x80;
        buf[pos++] = b;
    } while (len > 0);
    return pos;
}

uint16_t ElctinsIoTClient::_nextPacketId() {
    if (++_packetId == 0) _packetId = 1;
    return _packetId;
}

void ElctinsIoTClient::_log(const char* msg) {
    if (_debug) Serial.println(msg);
}

void ElctinsIoTClient::_log(const char* msg, const char* val) {
    if (_debug) { Serial.print(msg); Serial.println(val); }
}
