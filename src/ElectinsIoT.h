#pragma once

/*
 * ============================================================================
 * ElectinsIoT.h — Zero-dependency Async MQTT Library (v2.1.4)
 * ============================================================================
 *
 * Engine  : ElectinsMqtt (MQTT 3.1.1 built-in, tanpa dependensi eksternal)
 * TCP     : WiFiClient / WiFiClientSecure (bawaan ESP32/ESP8266 SDK)
 * Pump    : FreeRTOS task khusus (ESP32) / scheduled function (ESP8266)
 * Target  : ESP32 & ESP8266
 *
 * Developer Experience:
 *
 *   void setup() {
 *       mqtt.onConnect(onConnected);
 *       mqtt.subscribe("user/proj/cmd", onCmd);
 *       mqtt.begin("SSID", "pass", "iot.electins.id", 1883,
 *                  "ClientID", "user", "pass", "proj");
 *   }
 *   void loop() { }  // kosong — tidak ada mqtt.run()
 *
 * Library menangani otomatis:
 *   - Koneksi WiFi + MQTT
 *   - LWT "offline" ke <user>/<slug>/$status (retain=true)
 *   - Publish "online" saat connect (retain=true)
 *   - Heartbeat "online" berkala (dikelola dari konteks pemilik engine)
 *   - Auto-reconnect WiFi & MQTT
 *   - Re-subscribe semua topik setelah reconnect
 *   - TLS/SSL via WiFiClientSecure (opsional)
 *
 * Model eksekusi (anti-race):
 *   Engine dipompa dari SATU konteks pemilik socket — FreeRTOS task khusus
 *   di ESP32, scheduled function di ESP8266. publish()/subscribe() dari
 *   konteks mana pun aman karena hanya menaruh paket ke outbox (mutex),
 *   tidak menyentuh socket langsung.
 * ============================================================================
 */

#include <Arduino.h>
#include "ElectinsMqtt.h"

#if defined(ESP32)
  #include <WiFi.h>
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <Schedule.h>
#else
  #error "ElectinsIoT hanya mendukung ESP32 dan ESP8266."
#endif

// ─── Optional ArduinoJson support ─────────────────────────────────────────────
// Include ArduinoJson SEBELUM ElectinsIoT untuk mengaktifkan helper JSON
#if defined(ARDUINOJSON_VERSION_MAJOR)
  #define ELECTINS_JSON_ENABLED 1
  #include <ArduinoJson.h>
#endif

// ─── Konstanta ────────────────────────────────────────────────────────────────
#define ELECTINS_VERSION          "2.1.4"
#define ELECTINS_MAX_SUBS         16
#define ELECTINS_TOPIC_BUF_LEN    128
#define ELECTINS_STR_BUF_LEN      64
#define ELECTINS_HEARTBEAT_SEC    30   // interval heartbeat (detik)
#define ELECTINS_RECONNECT_SEC    5    // interval reconnect (detik)
#define ELECTINS_SERVICE_MS       5    // periode pump engine (ms)
#define ELECTINS_TASK_STACK       8192 // stack task MQTT (ESP32) — cukup untuk TLS
#define ELECTINS_TASK_PRIO        1    // prioritas task MQTT (ESP32)

// ─── QoS ─────────────────────────────────────────────────────────────────────
enum MqttQoS { QOS0 = 0, QOS1 = 1, QOS2 = 2 };

// ─── MqttParam — helper akses payload masuk ───────────────────────────────────
class MqttParam {
public:
    MqttParam(const char* data, size_t length) : _len(length) {
        size_t n = (length < sizeof(_str) - 1) ? length : sizeof(_str) - 1;
        memcpy(_str, data, n);
        _str[n] = '\0';
    }

    const char* asStr()   const { return _str; }
    int         asInt()   const { return atoi(_str); }
    float       asFloat() const { return atof(_str); }
    bool        asBool()  const {
        return strcmp(_str, "1")    == 0 ||
               strcmp(_str, "true") == 0 ||
               strcmp(_str, "on")   == 0;
    }
    size_t length() const { return _len; }

private:
    char   _str[128];
    size_t _len;
};

// ─── Tipe callback publik ────────────────────────────────────────────────────
typedef void (*MqttConnectCallback)();
typedef void (*MqttDisconnectCallback)();
typedef void (*MqttMessageCallback)(const char* topic, const char* payload,
                                    size_t length);
typedef void (*MqttTopicCallback)(const char* payload, size_t length);
typedef void (*MqttParamCallback)(MqttParam& param);

// ── Type-erasure untuk callback JSON ──────────────────────────────────────────
// Tipe ini SELALU terdefinisi (tidak tergantung ArduinoJson) supaya layout
// MqttSubscription dan signature method identik di semua translation unit —
// baik TU library (tanpa ArduinoJson) maupun TU sketsa (dengan ArduinoJson).
// Semua kode yang menyentuh JsonDocument bersifat header-only (lihat di bawah),
// jadi hanya dikompilasi di TU pengguna yang memang meng-include ArduinoJson.
typedef void (*MqttGenericFn)();
typedef void (*MqttJsonInvoker)(MqttGenericFn userCb, const char* topic,
                                const char* payload, size_t length);

#if defined(ELECTINS_JSON_ENABLED)
typedef void (*MqttJsonCallback)(const char* topic, JsonDocument& doc);

// Trampoline header-only — dikompilasi di TU pengguna (punya ArduinoJson).
// Cast antar function-pointer (well-defined oleh standar selama di-cast balik
// sebelum dipanggil).
inline void _electinsJsonTrampoline(MqttGenericFn userCb, const char* topic,
                                    const char* payload, size_t length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length)) return; // parse error → abaikan
    reinterpret_cast<MqttJsonCallback>(userCb)(topic, doc);
}
#endif

// ─── Subscription entry ───────────────────────────────────────────────────────
// Layout stabil di semua TU (tidak ada anggota ber-#if).
struct MqttSubscription {
    char               topic[ELECTINS_TOPIC_BUF_LEN];
    MqttTopicCallback  rawCallback;
    MqttParamCallback  paramCallback;
    MqttGenericFn      jsonUserCb;   // callback JSON pengguna (type-erased)
    MqttJsonInvoker    jsonInvoke;   // trampoline yang men-deserialize + memanggil
    uint8_t            qos;
    bool               active;
};

// ─── Kelas Utama ──────────────────────────────────────────────────────────────
class ElectinsIoT {
public:
    ElectinsIoT();
    ~ElectinsIoT() = default;

    // ─────────────────────────────────────────────────────────────────────────
    // KONFIGURASI OPSIONAL — panggil sebelum begin()
    // ─────────────────────────────────────────────────────────────────────────

    void setDebug(bool enable);
    void setKeepAlive(uint16_t seconds);
    void setReconnectInterval(uint16_t seconds);
    void setHeartbeatInterval(uint16_t seconds);

    // Override prefix pengguna untuk topik $status (opsional).
    // Mulai v2.1.3 prefix WAJIB diisi via parameter begin(); setter ini
    // hanya untuk skenario lanjut (mis. mengganti prefix saat runtime).
    // Jika dipanggil setelah begin(), $status akan dibangun ulang otomatis.
    void setUserPrefix(const char* prefix);

    // Aktifkan TLS/SSL (MQTT over port 8883)
    // Panggil sebelum begin()
    void setSecure(bool enable);

    // Skip verifikasi sertifikat (untuk broker self-signed)
    // Hanya berlaku jika setSecure(true) sudah dipanggil
    void setInsecure(bool insecure = true);

    // ─────────────────────────────────────────────────────────────────────────
    // CALLBACK — daftarkan sebelum begin()
    // ─────────────────────────────────────────────────────────────────────────

    void onConnect(MqttConnectCallback cb);
    void onDisconnect(MqttDisconnectCallback cb);
    void onMessage(MqttMessageCallback cb);

    // ─────────────────────────────────────────────────────────────────────────
    // SUBSCRIBE
    // ─────────────────────────────────────────────────────────────────────────

    bool subscribe(const char* topic, MqttQoS qos = QOS0);
    bool subscribe(const char* topic, MqttParamCallback cb, MqttQoS qos = QOS0);
    bool subscribe(const char* topic, MqttTopicCallback cb, MqttQoS qos = QOS0);
    bool unsubscribe(const char* topic);

#if defined(ELECTINS_JSON_ENABLED)
    // Header-only — dikompilasi di TU pengguna (punya ArduinoJson).
    bool subscribeJson(const char* topic, MqttJsonCallback cb,
                       MqttQoS qos = QOS0) {
        return _registerSub(topic, nullptr, nullptr,
                            reinterpret_cast<MqttGenericFn>(cb),
                            &_electinsJsonTrampoline, qos);
    }
#endif

    // ─────────────────────────────────────────────────────────────────────────
    // PUBLISH
    // ─────────────────────────────────────────────────────────────────────────

    bool publish(const char* topic, const char* payload,
                 bool retain = false, MqttQoS qos = QOS0);
    bool publish(const char* topic, int value,   bool retain = false);
    bool publish(const char* topic, float value, uint8_t decimals = 2,
                 bool retain = false);
    bool publish(const char* topic, bool value,  bool retain = false);

#if defined(ELECTINS_JSON_ENABLED)
    // Header-only — serialisasi terjadi di TU pengguna (punya ArduinoJson).
    bool publishJson(const char* topic, JsonDocument& doc,
                     bool retain = false, MqttQoS qos = QOS0) {
        if (!topic) return false;
        char buf[512];
        size_t len = serializeJson(doc, buf, sizeof(buf));
        if (len == 0) return false;
        return publish(topic, buf, retain, qos);
    }
#endif

    // Shorthand: mqtt << "topik:payload"
    ElectinsIoT& operator<<(const char* topicPayload);

    // ─────────────────────────────────────────────────────────────────────────
    // ENTRY POINT — panggil SEKALI di void setup()
    // ─────────────────────────────────────────────────────────────────────────
    //
    //  ssid, wifiPass   — WiFi credential
    //  mqttHost         — hostname / IP broker
    //  mqttPort         — port (1883 plain, 8883 TLS)
    //  clientId         — client ID unik perangkat
    //  mqttUser         — username MQTT (kredensial autentikasi ke broker)
    //  mqttPass         — password MQTT
    //  userPrefix       — prefix topik milik pengguna (mis. "ID-XXXXXXXX")
    //                     dipakai untuk topik $status: <userPrefix>/<projectSlug>/$status
    //                     PARAMETER WAJIB — tidak boleh kosong/nullptr.
    //                     Di setup multi-tenant, ini biasanya berbeda dengan mqttUser
    //                     (yang berisi kredensial broker mis. "PRJ-XXXXXXXX").
    //  projectSlug      — slug project (default "device")
    //
    void begin(const char* ssid,     const char* wifiPass,
               const char* mqttHost, uint16_t    mqttPort,
               const char* clientId,
               const char* mqttUser, const char* mqttPass,
               const char* userPrefix,
               const char* projectSlug = "device");

    // ─────────────────────────────────────────────────────────────────────────
    // STATUS
    // ─────────────────────────────────────────────────────────────────────────

    bool        connected()   { return _mqttEngine.connected(); }
    const char* statusTopic() const { return _statusTopic; }

private:
    // ── Engine ────────────────────────────────────────────────────────────────
    ElectinsMqtt  _mqttEngine;

#if defined(ESP32)
    TaskHandle_t  _taskHandle = nullptr;   // task pemilik socket
#elif defined(ESP8266)
    bool          _serviceScheduled = false;
#endif

    // ── State ─────────────────────────────────────────────────────────────────
    bool     _wifiHandlerSet  = false;
    bool     _debug           = false;
    bool     _secure          = false;
    bool     _insecure        = false; // default false — verifikasi sertifikat AKTIF
    uint16_t _keepAliveSec    = 15;
    uint16_t _reconnectSec    = ELECTINS_RECONNECT_SEC;
    uint16_t _heartbeatSec    = ELECTINS_HEARTBEAT_SEC;
    uint32_t _lastHeartbeat   = 0;

    // ── Buffer string internal ────────────────────────────────────────────────
    char _ssid[ELECTINS_STR_BUF_LEN]          = {0};
    char _wifiPass[ELECTINS_STR_BUF_LEN]      = {0};
    char _mqttHost[ELECTINS_STR_BUF_LEN]      = {0};
    char _clientId[ELECTINS_STR_BUF_LEN]      = {0};
    char _mqttUser[ELECTINS_STR_BUF_LEN]      = {0};
    char _mqttPass[ELECTINS_STR_BUF_LEN]      = {0};
    char _userPrefix[ELECTINS_STR_BUF_LEN]    = {0};  // prefix topik $status (opsional)
    char _projectSlug[ELECTINS_STR_BUF_LEN]   = {0};  // slug project (disimpan untuk rebuild $status)
    char _statusTopic[ELECTINS_TOPIC_BUF_LEN] = {0};
    uint16_t _mqttPort = 1883;

    // ── Subscriptions ─────────────────────────────────────────────────────────
    MqttSubscription _subs[ELECTINS_MAX_SUBS];
    uint8_t          _subCount = 0;

    // ── User callbacks ────────────────────────────────────────────────────────
    MqttConnectCallback    _connectCb    = nullptr;
    MqttDisconnectCallback _disconnectCb = nullptr;
    MqttMessageCallback    _messageCb    = nullptr;

    // ── Internal ──────────────────────────────────────────────────────────────
    void _setupWiFiHandlers();
    void _setupMqttCallbacks();
    void _startService();
    void _service();                 // pump engine + housekeeping (1 konteks)
#if defined(ESP32)
    static void _taskTrampoline(void* arg);
#endif
    void _connectToWiFi();
    void _resubscribeAll();
    void _publishOnline();
    void _onMqttConnected();
    void _onMqttDisconnected();
    void _onMqttMessage(const char* topic, const char* payload,
                        uint16_t length, uint8_t qos, bool retain);
    void _dispatchMessage(const char* topic, const char* payload, size_t len);
    bool _topicMatches(const char* filter, const char* topic) const;
    bool _registerSub(const char* topic,
                      MqttTopicCallback rawCb,
                      MqttParamCallback paramCb,
                      MqttGenericFn     jsonUserCb,
                      MqttJsonInvoker   jsonInvoke,
                      MqttQoS qos);
    void _log(const char* msg) const;
    void _log(const char* msg, const char* val) const;

    static ElectinsIoT* _instance;
};
