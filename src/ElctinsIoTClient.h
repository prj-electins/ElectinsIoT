#pragma once

#include <Arduino.h>
#include <Client.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32)
  #include <WiFi.h>
#endif

// ─── MQTT Packet Types ────────────────────────────────────────────────────────
#define MQTT_CONNECT     0x10
#define MQTT_CONNACK     0x20
#define MQTT_PUBLISH     0x30
#define MQTT_PUBACK      0x40
#define MQTT_PUBREC      0x50
#define MQTT_PUBREL      0x62
#define MQTT_PUBCOMP     0x70
#define MQTT_SUBSCRIBE   0x82
#define MQTT_SUBACK      0x90
#define MQTT_UNSUBSCRIBE 0xA2
#define MQTT_UNSUBACK    0xB0
#define MQTT_PINGREQ     0xC0
#define MQTT_PINGRESP    0xD0
#define MQTT_DISCONNECT  0xE0

// ─── Defaults ─────────────────────────────────────────────────────────────────
#define MQTT_KEEPALIVE   60
#define MQTT_TIMEOUT_MS  5000
#define MQTT_BUFFER_SIZE 512
#define MQTT_MAX_SUBS    16

// ─── QoS ─────────────────────────────────────────────────────────────────────
enum MqttQoS { QOS0 = 0, QOS1 = 1, QOS2 = 2 };

// ─── MqttParam — payload helper ───────────────────────────────────────────────
class MqttParam {
public:
    MqttParam(const uint8_t* data, uint16_t length) : _data(data), _len(length) {
        // Null-terminate untuk keamanan asStr()
        uint16_t copy = length < sizeof(_str) - 1 ? length : sizeof(_str) - 1;
        memcpy(_str, data, copy);
        _str[copy] = '\0';
    }

    const char*    asStr()   const { return _str; }
    int            asInt()   const { return atoi(_str); }
    float          asFloat() const { return atof(_str); }
    bool           asBool()  const {
        return (strcmp(_str, "1") == 0 || strcmp(_str, "true") == 0 || strcmp(_str, "on") == 0);
    }
    uint16_t       length()  const { return _len; }
    const uint8_t* data()    const { return _data; }

private:
    const uint8_t* _data;
    uint16_t       _len;
    char           _str[128]; // null-terminated copy untuk asStr()
};

// ─── Callback types ───────────────────────────────────────────────────────────
typedef void (*MqttCallback)(const char* topic, const uint8_t* payload, uint16_t length);
typedef void (*MqttTopicCallback)(const uint8_t* payload, uint16_t length);
typedef void (*MqttParamCallback)(MqttParam& param);
typedef void (*MqttConnectCallback)();
typedef void (*MqttDisconnectCallback)();

// ─── Subscription entry ───────────────────────────────────────────────────────
struct MqttSubscription {
    char               topic[64];
    MqttTopicCallback  callback;
    MqttParamCallback  paramCallback;
    uint8_t            qos;
    bool               active;
};

// ─── Main Class ───────────────────────────────────────────────────────────────
class ElctinsIoTClient {
public:
    ElctinsIoTClient(Client& client);
    ~ElctinsIoTClient();

    // ── Config ────────────────────────────────────────────────────────────────
    void setServer(const char* host, uint16_t port = 1883);
    void setCredentials(const char* user, const char* pass);
    void setClientId(const char* clientId);
    void setWill(const char* topic, const char* payload, bool retain = false, MqttQoS qos = QOS0);
    void setKeepAlive(uint16_t seconds);
    void setBufferSize(uint16_t size);
    void setDebug(bool enable);
    void enableReconnect(bool enable = true, uint32_t intervalMs = 5000);

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void onConnect(MqttConnectCallback cb);
    void onDisconnect(MqttDisconnectCallback cb);
    void onMessage(MqttCallback cb);

    // ── One-line begin — panggil SETELAH semua set/on dipanggil ──────────────
    bool begin(const char* ssid, const char* wifiPass,
               const char* host, uint16_t port,
               const char* clientId,
               const char* user = nullptr, const char* mqttPass = nullptr);

    // ── Connect ───────────────────────────────────────────────────────────────
    bool connect();
    bool connect(const char* clientId);
    bool connect(const char* clientId, const char* user, const char* pass);
    void disconnect();
    bool connected();

    // ── Publish ───────────────────────────────────────────────────────────────
    bool publish(const char* topic, const char* payload, bool retain = false, MqttQoS qos = QOS0);
    bool publish(const char* topic, int value,           bool retain = false);
    bool publish(const char* topic, float value,         uint8_t decimals = 2, bool retain = false);
    bool publish(const char* topic, bool value,          bool retain = false);
    bool publish(const char* topic, const uint8_t* payload, uint16_t length, bool retain = false, MqttQoS qos = QOS0);

    // Shorthand: mqtt << "topic:payload"
    ElctinsIoTClient& operator<<(const char* topicPayload);

    // ── Subscribe ─────────────────────────────────────────────────────────────
    bool subscribe(const char* topic, MqttQoS qos = QOS0);
    bool subscribe(const char* topic, MqttParamCallback cb, MqttQoS qos = QOS0);
    bool subscribe(const char* topic, MqttTopicCallback cb, MqttQoS qos = QOS0);
    bool unsubscribe(const char* topic);

    // ── Loop ──────────────────────────────────────────────────────────────────
    void run();    // alias loop()
    void loop();

private:
    Client&                 _client;
    const char*             _host           = nullptr;
    uint16_t                _port           = 1883;
    const char*             _clientId       = nullptr;
    const char*             _user           = nullptr;
    const char*             _pass           = nullptr;
    const char*             _willTopic      = nullptr;
    const char*             _willPayload    = nullptr;
    bool                    _willRetain     = false;
    MqttQoS                 _willQos        = QOS0;
    uint16_t                _keepAlive      = MQTT_KEEPALIVE;
    uint16_t                _bufferSize     = MQTT_BUFFER_SIZE;
    bool                    _debug          = false;

    MqttCallback            _globalCb       = nullptr;
    MqttConnectCallback     _connectCb      = nullptr;
    MqttDisconnectCallback  _disconnectCb   = nullptr;

    MqttSubscription        _subs[MQTT_MAX_SUBS];
    uint8_t                 _subCount       = 0;

    bool                    _connected              = false;
    bool                    _reconnect              = true;
    uint32_t                _reconnectInterval      = 5000;
    uint32_t                _lastReconnectAttempt   = 0;
    uint32_t                _lastPing               = 0;
    uint16_t                _packetId               = 1;

    uint8_t*                _buf            = nullptr;

    bool     _doConnect();
    void     _resubscribeAll();
    bool     _sendSubscribe(const char* topic, uint8_t qos);
    bool     _sendPacket(uint8_t* data, uint16_t len);
    bool     _waitFor(uint8_t type, uint32_t timeout = MQTT_TIMEOUT_MS);
    void     _processIncoming();
    void     _dispatchMessage(const char* topic, const uint8_t* payload, uint16_t length);
    bool     _topicMatches(const char* filter, const char* topic);
    uint16_t _writeString(uint8_t* buf, uint16_t pos, const char* str);
    uint16_t _encodeLength(uint8_t* buf, uint16_t pos, uint32_t len);
    uint16_t _nextPacketId();
    void     _log(const char* msg);
    void     _log(const char* msg, const char* val);
};
