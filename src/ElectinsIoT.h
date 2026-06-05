#pragma once

/*
 * ============================================================================
 * ElectinsIoT.h — Zero-dependency Async MQTT Library (v2.0.0)
 * ============================================================================
 *
 * Engine  : ElectinsMqtt (MQTT 3.1.1 built-in, tanpa dependensi eksternal)
 * TCP     : WiFiClient / WiFiClientSecure (bawaan ESP32/ESP8266 SDK)
 * Timer   : Ticker (bawaan ESP32/ESP8266 SDK)
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
 *   - Heartbeat "online" setiap 30 detik via Ticker
 *   - Auto-reconnect WiFi & MQTT
 *   - Re-subscribe semua topik setelah reconnect
 *   - TLS/SSL via WiFiClientSecure (opsional)
 * ============================================================================
 */

#include <Arduino.h>
#include <Ticker.h>
#include "ElectinsMqtt.h"

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
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
#define ELECTINS_VERSION          "2.0.0"
#define ELECTINS_MAX_SUBS         16
#define ELECTINS_TOPIC_BUF_LEN    128
#define ELECTINS_STR_BUF_LEN      64
#define ELECTINS_HEARTBEAT_SEC    30   // interval heartbeat (detik)
#define ELECTINS_RECONNECT_SEC    5    // interval reconnect (detik)
#define ELECTINS_POLL_MS          10   // interval poll TCP (ms)

// ─── QoS ─────────────────────────────────────────────────────────────────────
enum MqttQoS { QOS0 = 0, QOS1 = 1 };

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

#if defined(ELECTINS_JSON_ENABLED)
typedef void (*MqttJsonCallback)(const char* topic, JsonDocument& doc);
#endif

// ─── Subscription entry ───────────────────────────────────────────────────────
struct MqttSubscription {
    char               topic[ELECTINS_TOPIC_BUF_LEN];
    MqttTopicCallback  rawCallback;
    MqttParamCallback  paramCallback;
#if defined(ELECTINS_JSON_ENABLED)
    MqttJsonCallback   jsonCallback;
#endif
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
    bool subscribeJson(const char* topic, MqttJsonCallback cb,
                       MqttQoS qos = QOS0);
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
    bool publishJson(const char* topic, JsonDocument& doc,
                     bool retain = false, MqttQoS qos = QOS0);
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
    //  mqttUser         — username MQTT (digunakan untuk topik $status)
    //  mqttPass         — password MQTT
    //  projectSlug      — slug project (default "device")
    //                     topik $status = <mqttUser>/<projectSlug>/$status
    //
    void begin(const char* ssid,     const char* wifiPass,
               const char* mqttHost, uint16_t    mqttPort,
               const char* clientId,
               const char* mqttUser, const char* mqttPass,
               const char* projectSlug = "device");

    // ─────────────────────────────────────────────────────────────────────────
    // STATUS
    // ─────────────────────────────────────────────────────────────────────────

    bool        connected()   const { return _mqttEngine.connected(); }
    const char* statusTopic() const { return _statusTopic; }

private:
    // ── Engine ────────────────────────────────────────────────────────────────
    ElectinsMqtt  _mqttEngine;
    Ticker        _pollTicker;       // poll TCP setiap 10ms
    Ticker        _heartbeatTicker;  // publish "online" setiap N detik
    Ticker        _reconnectTicker;  // coba reconnect setiap N detik

    // ── State ─────────────────────────────────────────────────────────────────
    bool     _wifiHandlerSet  = false;
    bool     _mqttConnecting  = false;
    bool     _debug           = false;
    bool     _secure          = false;
    bool     _insecure        = false; // default false — verifikasi sertifikat AKTIF
    uint16_t _keepAliveSec    = 15;
    uint16_t _reconnectSec    = ELECTINS_RECONNECT_SEC;
    uint16_t _heartbeatSec    = ELECTINS_HEARTBEAT_SEC;

    // ── Buffer string internal ────────────────────────────────────────────────
    char _ssid[ELECTINS_STR_BUF_LEN]          = {0};
    char _wifiPass[ELECTINS_STR_BUF_LEN]      = {0};
    char _mqttHost[ELECTINS_STR_BUF_LEN]      = {0};
    char _clientId[ELECTINS_STR_BUF_LEN]      = {0};
    char _mqttUser[ELECTINS_STR_BUF_LEN]      = {0};
    char _mqttPass[ELECTINS_STR_BUF_LEN]      = {0};
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
    void _connectToWiFi();
    void _connectToMqtt();
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
#if defined(ELECTINS_JSON_ENABLED)
                      MqttJsonCallback  jsonCb,
#endif
                      MqttQoS qos);
    void _log(const char* msg) const;
    void _log(const char* msg, const char* val) const;

#if defined(ELECTINS_JSON_ENABLED)
    void _dispatchJson(MqttJsonCallback cb, const char* topic,
                       const char* payload, size_t length);
#endif

    static ElectinsIoT* _instance;
};
