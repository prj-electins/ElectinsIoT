/*
 * ElectinsIoT.cpp — Zero-dependency Async MQTT Library (v2.1.2)
 *
 * Menggunakan ElectinsMqtt engine internal (single-owner + outbox).
 * Engine dipompa dari SATU konteks: FreeRTOS task khusus (ESP32) atau
 * scheduled function (ESP8266). void loop() pengguna tetap kosong.
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

void ElectinsIoT::setUserPrefix(const char* prefix) {
    if (!prefix) { _userPrefix[0] = '\0'; return; }
    strncpy(_userPrefix, prefix, sizeof(_userPrefix) - 1);
    _userPrefix[sizeof(_userPrefix) - 1] = '\0';
}

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

    // Bangun topik $status: <userPrefix>/<projectSlug>/$status
    // Jika userPrefix tidak diset (via setUserPrefix), fallback ke mqttUser
    // agar tetap kompatibel dengan kode yang sudah ada.
    const char* prefix = (_userPrefix[0] != '\0') ? _userPrefix : _mqttUser;
    snprintf(_statusTopic, sizeof(_statusTopic),
             "%s/%s/$status",
             prefix,
             (projectSlug && projectSlug[0] != '\0') ? projectSlug : "device");
    _log("[Electins] Status topic: ", _statusTopic);

    // Setup WiFi event dan MQTT callbacks (sekali saja — cegah double-register)
    if (!_wifiHandlerSet) {
        _setupWiFiHandlers();
        _setupMqttCallbacks();
        _wifiHandlerSet = true;
    }

    // Konfigurasi MQTT engine
    _mqttEngine.begin();   // buat mutex (ESP32) — aman dipanggil berulang
    _mqttEngine.setServer(_mqttHost, _mqttPort);
    _mqttEngine.setClientId(_clientId);
    _mqttEngine.setKeepAlive(_keepAliveSec);
    _mqttEngine.setReconnectInterval(_reconnectSec);
    _mqttEngine.setSecure(_secure);
    _mqttEngine.setInsecure(_insecure);

    if (_mqttUser[0] != '\0')
        _mqttEngine.setCredentials(_mqttUser,
                                   _mqttPass[0] != '\0' ? _mqttPass : nullptr);

    // LWT otomatis
    _mqttEngine.setWill(_statusTopic, "offline", true, 0);
    _log("[Electins] LWT: ", _statusTopic);

    // Jalankan pump engine di satu konteks pemilik:
    //   ESP32     → FreeRTOS task khusus (stack besar, aman untuk TLS)
    //   ESP8266   → scheduled function (dijalankan di konteks loop, bukan ISR)
    _startService();

    // Mulai koneksi WiFi (engine akan connect MQTT otomatis saat WiFi siap)
    _connectToWiFi();
}

// ─── Pump engine (single owner) ────────────────────────────────────────────────

void ElectinsIoT::_startService() {
#if defined(ESP32)
    if (_taskHandle) return;
    xTaskCreatePinnedToCore(
        _taskTrampoline, "electinsMqtt",
        ELECTINS_TASK_STACK, this, ELECTINS_TASK_PRIO,
        &_taskHandle, 1 /* core 1 — sama dengan loop() */);
#elif defined(ESP8266)
    if (_serviceScheduled) return;
    _serviceScheduled = true;
    schedule_recurrent_function_us([]() -> bool {
        if (ElectinsIoT::_instance) ElectinsIoT::_instance->_service();
        return true; // tetap terjadwal
    }, (uint32_t)ELECTINS_SERVICE_MS * 1000UL);
#endif
}

#if defined(ESP32)
void ElectinsIoT::_taskTrampoline(void* arg) {
    ElectinsIoT* self = static_cast<ElectinsIoT*>(arg);
    for (;;) {
        self->_service();
        vTaskDelay(pdMS_TO_TICKS(ELECTINS_SERVICE_MS));
    }
}
#endif

void ElectinsIoT::_service() {
    _mqttEngine.loop();

    // Heartbeat "online" — dijalankan di konteks pemilik yang sama (aman)
    if (_heartbeatSec > 0 && _mqttEngine.connected()) {
        uint32_t now = millis();
        if ((uint32_t)(now - _lastHeartbeat) >= (uint32_t)_heartbeatSec * 1000UL) {
            _lastHeartbeat = now;
            _publishOnline();
        }
    }
}

// ─── WiFi event handlers ──────────────────────────────────────────────────────

void ElectinsIoT::_setupWiFiHandlers() {
#if defined(ESP32)
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t /*info*/) {
        ElectinsIoT* self = ElectinsIoT::_instance;
        if (!self) return;

        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            self->_log("[WiFi] Terhubung ke ip ", WiFi.localIP().toString().c_str());
            // Tidak perlu trigger MQTT di sini — engine connect otomatis saat
            // WiFi siap. WiFi-event TIDAK menyentuh socket (cegah race).
        }
        else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            self->_log("[WiFi] Terputus — menunggu auto-reconnect");
            // Hanya urus WiFi. Engine akan mendeteksi socket mati di loop()-nya.
            WiFi.reconnect();
        }
    });

#elif defined(ESP8266)
    static WiFiEventHandler _evGotIP =
        WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP&) {
            ElectinsIoT* self = ElectinsIoT::_instance;
            if (!self) return;
            self->_log("[WiFi] Terhubung ke ip ", WiFi.localIP().toString().c_str());
        });

    static WiFiEventHandler _evDiscon =
        WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected&) {
            ElectinsIoT* self = ElectinsIoT::_instance;
            if (!self) return;
            self->_log("[WiFi] Terputus — menunggu auto-reconnect");
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
    _log("[MQTT] Tersambung ke broker");

    _resubscribeAll();
    _publishOnline();
    _lastHeartbeat = millis();   // reset jadwal heartbeat (dikelola _service)

    if (_connectCb) _connectCb();
}

void ElectinsIoT::_onMqttDisconnected() {
    _log("[MQTT] Terputus — engine akan reconnect otomatis");
    // Tidak ada ticker untuk di-detach. Engine menangani reconnect sendiri
    // di loop()-nya (dengan backoff _reconnectSec) begitu WiFi tersedia.
    if (_disconnectCb) _disconnectCb();
}

void ElectinsIoT::_onMqttMessage(const char* topic, const char* payload,
                                  uint16_t length, uint8_t /*qos*/,
                                  bool /*retain*/) {
    _dispatchMessage(topic, payload, length);
}

// ─── WiFi & MQTT connect ──────────────────────────────────────────────────────

void ElectinsIoT::_connectToWiFi() {
    // Jika sudah tersambung, engine akan langsung lanjut ke MQTT di loop()-nya.
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0) {
        _log("[WiFi] Terhubung ke ip ", WiFi.localIP().toString().c_str());
        return;
    }
    _log("[WiFi] Menghubungkan ke wifi (SSID): ", _ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(_ssid, _wifiPass[0] != '\0' ? _wifiPass : nullptr);
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
                                MqttGenericFn     jsonUserCb,
                                MqttJsonInvoker   jsonInvoke,
                                MqttQoS qos) {
    if (!topic) return false;

    // Update entry yang sudah ada
    for (uint8_t i = 0; i < _subCount; i++) {
        if (strcmp(_subs[i].topic, topic) == 0) {
            _subs[i].rawCallback   = rawCb;
            _subs[i].paramCallback = paramCb;
            _subs[i].jsonUserCb    = jsonUserCb;
            _subs[i].jsonInvoke    = jsonInvoke;
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
    _subs[idx].jsonUserCb    = jsonUserCb;
    _subs[idx].jsonInvoke    = jsonInvoke;
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
    return _registerSub(topic, nullptr, nullptr, nullptr, nullptr, qos);
}

bool ElectinsIoT::subscribe(const char* topic, MqttParamCallback cb, MqttQoS qos) {
    return _registerSub(topic, nullptr, cb, nullptr, nullptr, qos);
}

bool ElectinsIoT::subscribe(const char* topic, MqttTopicCallback cb, MqttQoS qos) {
    return _registerSub(topic, cb, nullptr, nullptr, nullptr, qos);
}

// subscribeJson() & publishJson() bersifat header-only (lihat ElectinsIoT.h)
// agar kode ArduinoJson hanya dikompilasi di TU pengguna.

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
        } else if (_subs[i].jsonInvoke && _subs[i].jsonUserCb) {
            // Trampoline (header-only) men-deserialize lalu memanggil callback JSON.
            _subs[i].jsonInvoke(_subs[i].jsonUserCb, topic, payload, len);
        }
    }

    if (_messageCb) _messageCb(topic, payload, len);
}

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
