#include <ElectinsIoT.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32)
  #include <WiFi.h>
#endif

// ─── Configuration ────────────────────────────────────────────────────────────
const char* WIFI_SSID  = "YourSSID";
const char* WIFI_PASS  = "YourPassword";
const char* MQTT_HOST  = "broker.example.com";
const char* MQTT_USER  = "username";
const char* MQTT_PASS  = "password";
const uint16_t MQTT_PORT = 1883;

// ─── Topics ───────────────────────────────────────────────────────────────────
const char* TOPIC_STATUS  = "device/status";
const char* TOPIC_CMD     = "device/cmd";
const char* TOPIC_CONFIG  = "device/config";
const char* TOPIC_TEMP    = "device/temp";
const char* TOPIC_UPTIME  = "device/uptime";
const char* TOPIC_ALL     = "device/#";

WiFiClient wifiClient;
ElectinsIoT mqtt(wifiClient);

// ─── Per-topic handlers ───────────────────────────────────────────────────────
void onCmd(MqttParam& param) {
    Serial.printf("[CMD] %s\n", param.asStr());
    if (strcmp(param.asStr(), "restart") == 0) ESP.restart();
    if (strcmp(param.asStr(), "status")  == 0) mqtt.publish(TOPIC_STATUS, "running");
}

void onConfig(MqttParam& param) {
    Serial.printf("[CONFIG] Received %d bytes\n", param.length());
}

void onTemp(MqttParam& param) {
    Serial.printf("[TEMP] %.2f\n", param.asFloat());
}

void onWildcard(MqttParam& param) {
    Serial.printf("[device/#] len=%d\n", param.length());
}

// ─── Connect callback ─────────────────────────────────────────────────────────
void onConnected() {
    Serial.println("[MQTT] Connected!");
    mqtt.publish(TOPIC_STATUS, "online", true);

    mqtt.subscribe(TOPIC_CMD,    onCmd,      QOS1);
    Serial.print("[MQTT] Subscribed: "); Serial.println(TOPIC_CMD);

    mqtt.subscribe(TOPIC_CONFIG, onConfig,   QOS2);
    Serial.print("[MQTT] Subscribed: "); Serial.println(TOPIC_CONFIG);

    mqtt.subscribe(TOPIC_TEMP,   onTemp,     QOS0);
    Serial.print("[MQTT] Subscribed: "); Serial.println(TOPIC_TEMP);

    mqtt.subscribe(TOPIC_ALL,    onWildcard, QOS0);
    Serial.print("[MQTT] Subscribed: "); Serial.println(TOPIC_ALL);
}

void onDisconnected() {
    Serial.println("[MQTT] Disconnected!");
}

// ─── Global fallback — called for every incoming message ─────────────────────
void onMessage(const char* topic, const uint8_t* payload, uint16_t length) {
    Serial.printf("[MQTT] %s => %.*s\n", topic, length, (char*)payload);
}

void reconnectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    Serial.print("[WiFi] Reconnecting");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) Serial.println(" OK");
    else Serial.println(" Failed");
}

void setup() {
    Serial.begin(115200);

    mqtt.setWill(TOPIC_STATUS, "offline", true, QOS1);
    mqtt.setKeepAlive(30);
    mqtt.setDebug(true);
    mqtt.onConnect(onConnected);
    mqtt.onDisconnect(onDisconnected);
    mqtt.onMessage(onMessage);

    mqtt.begin(WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, "DeviceID-Advanced", MQTT_USER, MQTT_PASS);
}

void loop() {
    reconnectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;

    mqtt.run();

    // Publish sensor data every 10 seconds
    static uint32_t last = 0;
    if (millis() - last >= 10000) {
        last = millis();
        mqtt.publish(TOPIC_TEMP,   25.0f + random(0, 50) / 10.0f);
        mqtt.publish(TOPIC_UPTIME, (int)(millis() / 1000));
        mqtt.publish(TOPIC_STATUS, "running", false, QOS1);
        mqtt << "device/hello:world";
    }
}
