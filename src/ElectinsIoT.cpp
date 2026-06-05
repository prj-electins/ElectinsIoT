/*
 * ElectinsIoT.cpp — Zero-dependency Async MQTT Library (v2.0.0)
 *
 * Menggunakan ElectinsMqtt engine internal.
 * TCP polling via Ticker setiap 10ms — void loop() pengguna tetap kosong.
 */

#include "ElectinsIoT.h"
#include <stdio.h>
#include <string.h>

// ─── Static instance pointer ──────────────────────────────────────────────────
ElectinsIoT* ElectinsIoT::_instance = nullptr;

// ─── Constructor ──────────────────────────────────────────────────────────────

ElectinsIoT::ElectinsIoT() {
    memset(_subs, 0, sizeof(_subs));
    _instance = this;
}

// ─── Konfigurasi ─────────────────────────────────────────────────────────────

void ElectinsIoT::setDebug(bool e)             { _debug        = e; }
void ElectinsIoT::setKeepAlive(uint16_t s)     { _keepAliveSec = s > 0 ? s : 15; }
void ElectinsIoT::setReconnectInterval(uint16_t s) { _reconnectSec = s > 0 ? s : 1; }
void ElectinsIoT::setHeartbeatInterval(uint16_t s) { _heartbeatSec = s > 0 ? s : 30; }
void ElectinsIoT::setSecure(bool e)            { _secure    = e; }
void ElectinsIoT::setInsecure(bool i)          { _insecure  = i; }

// ─── Callback ────────────────────────────────────────────────────────────────

void ElectinsIoT::onConnect(MqttConnectCallback cb)       { _connectCb    = cb; }
void ElectinsIoT::onDisconnect(MqttDisconnectCallback cb) { _disconnectCb = cb; }
void ElectinsIoT::onMessage(MqttMessageCallback cb)       { _messageCb    = cb; }

// ─── ENTRY POINT ─────────────────────────────────────────────────────────────

void ElectinsIoT::begin(const char* ssid,     const char* wifiPass,
                        const char* mqttHost, uint16_t    mqttPort,
                        const char* clientId,
                        const char* mqttUser, const char* mqttPass,
                        const char* projectSlug) {

    if (!ssid || !mqttHost || !clientId || !mqttUser) {
        _log("[Electins] begin(): parameter wajib tidak boleh null");
        return;
    }

    // Salin semua string ke buffer internal
    strncpy(_ssid,     ssid,                  sizeof(_ssid)     - 1);
    strncpy(_wifiPass, wifiPass  ? wifiPass  : "", sizeof(_wifiPass) - 1);
    strncpy(_mqttHost, mqttHost,               sizeof(_mqttHost) - 1);
    strncpy(_clientId, clientId,               sizeof(_clientId) - 1);
    strncpy(_mqttUser, mqttUser,               sizeof(_mqttUser) - 1);
    strncpy(_mqttPass, mqttPass  ? mqttPass  : "", sizeof(_mqttPass) - 1);
    _ssid[sizeof(_ssid)-1]         = '\0';
    _wifiPass[sizeof(_wifiPass)-1] = '\0';
    _mqttHost[sizeof(_mqttHost)-1] = '\0';
    _clientId[sizeof(_clientId)-1] = '\0';
    _mqttUser[sizeof(_mqttUser)-1] = '\0';
    _mqttPass[sizeof(_mqttPass)-1] = '\0';
    _mqttPort = mqttPort;

    // Bangun topik $status
    snprintf(_statusTopic, sizeof(_statusTopic),
             "%s/%s/$status",
             _mqttUser,
             (projectSlug && projectSlug[0] != '\0') ? projectSlug : "device");
    _log("[Electins] Status topic: ", _statusTopic);

    // Setup WiFi event dan MQTT callbacks (sekali saja — cegah double-register)
    if (!_wifiHandlerSet) {
        _setupWiFiHandlers();
        _setupMqttCallbacks();
        _wifiHandlerSet = true;
    }

    // Konfigurasi MQTT engine
    _mqttEngine.setServer(_mqttHost, _mqttPort);
    _mqttEngine.setClientId(_clientId);
    _mqttEngine.setKeepAlive(_keepAliveSec);
    _mqttEngine.setSecure(_secure);
    _mqttEngine.setInsecure(_insecure);

    if (_mqttUser[0] != '\0')
        _mqttEngine.setCredentials(_mqttUser,
                                   _mqttPass[0] != '\0' ? _mqttPass : nullptr);

    // LWT otomatis
    _mqttEngine.setWill(_statusTopic, "offline", true, 0);
    _log("[Electins] LWT: ", _statusTopic);

    // Mulai polling Ticker (10ms — non-blocking TCP read)
    // Detach dulu sebelum attach agar tidak double-attach jika begin() dipanggil ulang
    _pollTicker.detach();
    _pollTicker.attach_ms(ELECTINS_POLL_MS, []() {
        if (ElectinsIoT::_instance)
            ElectinsIoT::_instance->_mqttEngine.poll();
    });

    // Mulai koneksi WiFi
    _connectToWiFi();
}

// ─── WiFi event handlers ──────────────────────────────────────────────────────

void ElectinsIoT::_setupWiFiHandlers() {
#if defined(ESP32)
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t /*info*/) {
        ElectinsIoT* self = ElectinsIoT::_instance;
        if (!self) return;

        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            self->_log("[WiFi] Tersambung → ", WiFi.localIP().toString().c_str());
            self->_connectToMqtt();
        }
        else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            self->_log("[WiFi] Terputus — jadwalkan reconnect");
            self->_mqttConnecting = false;
            self->_heartbeatTicker.detach();
            self->_reconnectTicker.detach();
            self->_reconnectTicker.attach(
                (float)self->_reconnectSec,
                []() {
                    if (ElectinsIoT::_instance)
                        ElectinsIoT::_instance->_connectToWiFi();
                }
            );
        }
    });

#elif defined(ESP8266)
    static WiFiEventHandler _evGotIP =
        WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP&) {
            ElectinsIoT* self = ElectinsIoT::_instance;
            if (!self) return;
            self->_log("[WiFi] Tersambung → ", WiFi.localIP().toString().c_str());
            self->_connectToMqtt();
        });

    static WiFiEventHandler _evDiscon =
        WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected&) {
            ElectinsIoT* self = ElectinsIoT::_instance;
            if (!self) return;
            self->_log("[WiFi] Terputus — jadwalkan reconnect");
            self->_mqttConnecting = false;
            self->_heartbeatTicker.detach();
            self->_reconnectTicker.detach();
            self->_reconnectTicker.attach(
                (float)self->_reconnectSec,
                []() {
                    if (ElectinsIoT::_instance)
                        ElectinsIoT::_instance->_connectToWiFi();
                }
            );
        });
#endif
}

// ─── MQTT internal callbacks ──────────────────────────────────────────────────

void ElectinsIoT::_setupMqttCallbacks() {
    _mqttEngine.onConnect([]() {
        if (ElectinsIoT::_instance)
            ElectinsIoT::_instance->_onMqttConnected();
    });
    _mqttEngine.onDisconnect([]() {
        if (ElectinsIoT::_instance)
            ElectinsIoT::_instance->_onMqttDisconnected();
    });
    _mqttEngine.onMessage([](const char* topic, const char* payload,
                              uint16_t length, uint8_t qos, bool retain) {
        if (ElectinsIoT::_instance)
            ElectinsIoT::_instance->_onMqttMessage(topic, payload,
                                                    length, qos, retain);
    });
}

void ElectinsIoT::_onMqttConnected() {
    _mqttConnecting = false;
    _reconnectTicker.detach();
    _log("[MQTT] Tersambung ke broker");

    _resubscribeAll();
    _publishOnline();

    _heartbeatTicker.attach(
        (float)_heartbeatSec,
        []() {
            if (ElectinsIoT::_instance)
                ElectinsIoT::_instance->_publishOnline();
        }
    );

    if (_connectCb) _connectCb();
}

void ElectinsIoT::_onMqttDisconnected() {
    _mqttConnecting = false;
    _heartbeatTicker.detach();
    _log("[MQTT] Terputus — jadwalkan reconnect");

    if (WiFi.isConnected()) {
        _reconnectTicker.detach();
        _reconnectTicker.attach(
            (float)_reconnectSec,
            []() {
                if (ElectinsIoT::_instance)
                    ElectinsIoT::_instance->_connectToMqtt();
            }
        );
    }

    if (_disconnectCb) _disconnectCb();
}

void ElectinsIoT::_onMqttMessage(const char* topic, const char* payload,
                                  uint16_t length, uint8_t /*qos*/,
                                  bool /*retain*/) {
    _dispatchMessage(topic, payload, length);
}

// ─── WiFi & MQTT connect ──────────────────────────────────────────────────────

void ElectinsIoT::_connectToWiFi() {
    _reconnectTicker.detach();
    _log("[WiFi] Menghubungkan ke: ", _ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _wifiPass[0] != '\0' ? _wifiPass : nullptr);
}

void ElectinsIoT::_connectToMqtt() {
    if (_mqttEngine.connected() || _mqttConnecting) return;
    _reconnectTicker.detach();
    _mqttConnecting = true;
    _log("[MQTT] Menghubungkan ke broker...");

    // connect() melakukan TCP + MQTT handshake.
    // Dipanggil dari Ticker callback — blocking singkat (max EMQTT_CONNACK_WAIT ms)
    // tapi ini hanya terjadi saat (re)connect, bukan setiap poll.
    bool ok = _mqttEngine.connect();
    if (!ok) {
        _mqttConnecting = false;
        _log("[MQTT] Koneksi gagal — akan dicoba lagi");
        // Jadwalkan retry
        _reconnectTicker.detach();
        _reconnectTicker.attach(
            (float)_reconnectSec,
            []() {
                if (ElectinsIoT::_instance)
                    ElectinsIoT::_instance->_connectToMqtt();
            }
        );
    }
    // Jika ok: _onMqttConnected() sudah dipanggil dari dalam engine
}

// ─── Publish "online" ─────────────────────────────────────────────────────────

void ElectinsIoT::_publishOnline() {
    if (!_mqttEngine.connected() || _statusTopic[0] == '\0') return;
    _mqttEngine.publish(_statusTopic, "online",
                        strlen("online"), true /*retain*/, 0 /*QoS*/);
    _log("[MQTT] status 'online' → ", _statusTopic);
}

// ─── Re-subscribe setelah reconnect ──────────────────────────────────────────

void ElectinsIoT::_resubscribeAll() {
    for (uint8_t i = 0; i < _subCount; i++) {
        if (_subs[i].active) {
            _mqttEngine.subscribe(_subs[i].topic, _subs[i].qos);
            _log("[MQTT] Subscribe: ", _subs[i].topic);
        }
    }
}

// ─── Subscribe ────────────────────────────────────────────────────────────────

bool ElectinsIoT::_registerSub(const char* topic,
                                MqttTopicCallback rawCb,
                                MqttParamCallback paramCb,
#if defined(ELECTINS_JSON_ENABLED)
                                MqttJsonCallback  jsonCb,
#endif
                                MqttQoS qos) {
    if (!topic) return false;

    // Update entry yang sudah ada
    for (uint8_t i = 0; i < _subCount; i++) {
        if (strcmp(_subs[i].topic, topic) == 0) {
            _subs[i].rawCallback   = rawCb;
            _subs[i].paramCallback = paramCb;
#if defined(ELECTINS_JSON_ENABLED)
            _subs[i].jsonCallback  = jsonCb;
#endif
            _subs[i].qos    = (uint8_t)qos;
            _subs[i].active = true;
            if (_mqttEngine.connected()) {
                _mqttEngine.subscribe(topic, (uint8_t)qos);
                _log("[MQTT] Subscribe: ", topic);
            }
            return true;
        }
    }

    // Tambah entry baru
    if (_subCount >= ELECTINS_MAX_SUBS) {
        _log("[MQTT] Batas maksimum subscription tercapai");
        return false;
    }

    uint8_t idx = _subCount;
    strncpy(_subs[idx].topic, topic, sizeof(_subs[0].topic) - 1);
    _subs[idx].topic[sizeof(_subs[0].topic) - 1] = '\0';
    _subs[idx].rawCallback   = rawCb;
    _subs[idx].paramCallback = paramCb;
#if defined(ELECTINS_JSON_ENABLED)
    _subs[idx].jsonCallback  = jsonCb;
#endif
    _subs[idx].qos    = (uint8_t)qos;
    _subs[idx].active = true;

    if (_mqttEngine.connected()) {
        _mqttEngine.subscribe(topic, (uint8_t)qos);
        _log("[MQTT] Subscribe: ", topic);
    }
    _subCount++;
    return true;
}

bool ElectinsIoT::subscribe(const char* topic, MqttQoS qos) {
    return _registerSub(topic, nullptr, nullptr,
#if defined(ELECTINS_JSON_ENABLED)
                        nullptr,
#endif
                        qos);
}

bool ElectinsIoT::subscribe(const char* topic, MqttParamCallback cb, MqttQoS qos) {
    return _registerSub(topic, nullptr, cb,
#if defined(ELECTINS_JSON_ENABLED)
                        nullptr,
#endif
                        qos);
}

bool ElectinsIoT::subscribe(const char* topic, MqttTopicCallback cb, MqttQoS qos) {
    return _registerSub(topic, cb, nullptr,
#if defined(ELECTINS_JSON_ENABLED)
                        nullptr,
#endif
                        qos);
}

#if defined(ELECTINS_JSON_ENABLED)
bool ElectinsIoT::subscribeJson(const char* topic, MqttJsonCallback cb,
                                 MqttQoS qos) {
    return _registerSub(topic, nullptr, nullptr, cb, qos);
}
#endif

bool ElectinsIoT::unsubscribe(const char* topic) {
    if (!topic) return false;
    for (uint8_t i = 0; i < _subCount; i++) {
        if (strcmp(_subs[i].topic, topic) == 0) {
            _subs[i].active = false;
            if (_mqttEngine.connected()) {
                _mqttEngine.unsubscribe(topic);
                _log("[MQTT] Unsubscribe: ", topic);
            }
            return true;
        }
    }
    return false;
}

// ─── Publish ──────────────────────────────────────────────────────────────────

bool ElectinsIoT::publish(const char* topic, const char* payload,
                           bool retain, MqttQoS qos) {
    if (!_mqttEngine.connected() || !topic || !payload) return false;
    uint16_t pid = _mqttEngine.publish(topic, payload,
                                       strlen(payload), retain, (uint8_t)qos);
    if (pid > 0) _log("[MQTT] Publish → ", topic);
    return pid > 0;
}

bool ElectinsIoT::publish(const char* topic, int value, bool retain) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    return publish(topic, buf, retain, QOS0);
}

bool ElectinsIoT::publish(const char* topic, float value,
                           uint8_t decimals, bool retain) {
    char buf[24];
    dtostrf(value, 1, decimals, buf);
    return publish(topic, buf, retain, QOS0);
}

bool ElectinsIoT::publish(const char* topic, bool value, bool retain) {
    return publish(topic, value ? "true" : "false", retain, QOS0);
}

#if defined(ELECTINS_JSON_ENABLED)
bool ElectinsIoT::publishJson(const char* topic, JsonDocument& doc,
                               bool retain, MqttQoS qos) {
    if (!topic) return false;
    char buf[512];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0) return false;
    return publish(topic, buf, retain, qos);
}
#endif

ElectinsIoT& ElectinsIoT::operator<<(const char* topicPayload) {
    if (!topicPayload) return *this;
    char tmp[256];
    strncpy(tmp, topicPayload, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char* sep = strchr(tmp, ':');
    if (sep) { *sep = '\0'; publish(tmp, sep + 1); }
    return *this;
}

// ─── Message dispatch ─────────────────────────────────────────────────────────

void ElectinsIoT::_dispatchMessage(const char* topic, const char* payload,
                                    size_t len) {
    for (uint8_t i = 0; i < _subCount; i++) {
        if (!_subs[i].active) continue;
        if (!_topicMatches(_subs[i].topic, topic)) continue;

        if (_subs[i].paramCallback) {
            MqttParam param(payload, len);
            _subs[i].paramCallback(param);
        } else if (_subs[i].rawCallback) {
            _subs[i].rawCallback(payload, len);
        }
#if defined(ELECTINS_JSON_ENABLED)
        else if (_subs[i].jsonCallback) {
            _dispatchJson(_subs[i].jsonCallback, topic, payload, len);
        }
#endif
    }

    if (_messageCb) _messageCb(topic, payload, len);
}

#if defined(ELECTINS_JSON_ENABLED)
void ElectinsIoT::_dispatchJson(MqttJsonCallback cb, const char* topic,
                                 const char* payload, size_t length) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (!err) cb(topic, doc);
    else _log("[JSON] Parse error: ", topic);
}
#endif

// ─── Topic wildcard matching ──────────────────────────────────────────────────

bool ElectinsIoT::_topicMatches(const char* filter, const char* topic) const {
    if (!filter || !topic) return false;
    const char* f = filter;
    const char* t = topic;
    while (*f && *t) {
        if (*f == '#') return true;
        if (*f == '+') {
            while (*t && *t != '/') t++;
            f++;
            continue;
        }
        if (*f != *t) return false;
        f++; t++;
    }
    if (*f == '#') return true;
    return (*f == '\0' && *t == '\0');
}

// ─── Debug log ────────────────────────────────────────────────────────────────

void ElectinsIoT::_log(const char* msg) const {
    if (_debug) Serial.println(msg);
}

void ElectinsIoT::_log(const char* msg, const char* val) const {
    if (_debug) { Serial.print(msg); Serial.println(val); }
}
