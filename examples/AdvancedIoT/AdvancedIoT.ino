/**
 * AdvancedIoT.ino — ElectinsIoT v2.1.3 Advanced Example
 * ───────────────────────────────────────────────────
 * Demonstrasi fitur lengkap:
 *   - Multi-topic subscribe + per-topic callback
 *   - QoS 0, 1, 2
 *   - Wildcard subscribe (#)
 *   - Global fallback onMessage()
 *   - onDisconnect callback
 *   - statusTopic() helper
 *   - Shorthand operator <<
 *
 * Dependensi: built-in SDK ESP32/ESP8266
 */

#include <ElectinsIoT.h>

// ─── Konfigurasi ──────────────────────────────────────────────────────────────
const char*    WIFI_SSID    = "WIFI_SSID";
const char*    WIFI_PASS    = "WIFI_PASSWORD";
const char*    MQTT_HOST    = "iot.electins.id";
const char*    MQTT_USER    = "PRJ-XXXXXXXX";
const char*    MQTT_PASS    = "PASSWORD";
const char*    USER_PREFIX  = "ID-XXXXXXXX";
const char*    PROJECT_SLUG = "project-slug";
const uint16_t MQTT_PORT    = 1883;

// ─── Topik ────────────────────────────────────────────────────────────────────
const char* TOPIC_CMD    = "ID-XXXXXXXX/project-slug/cmd";
const char* TOPIC_CONFIG = "ID-XXXXXXXX/project-slug/config";
const char* TOPIC_TEMP   = "ID-XXXXXXXX/project-slug/temp";
const char* TOPIC_UPTIME = "ID-XXXXXXXX/project-slug/uptime";
const char* TOPIC_ALL    = "ID-XXXXXXXX/project-slug/#";

ElectinsIoT mqtt;

// ─── Per-topic callbacks ──────────────────────────────────────────────────────

void onCmd(MqttParam& param) {
    Serial.printf("[CMD] %s\n", param.asStr());
    if (strcmp(param.asStr(), "restart") == 0) {
        Serial.println("[CMD] Restarting...");
        delay(300);
        ESP.restart();
    }
    if (strcmp(param.asStr(), "status") == 0) {
        mqtt.publish(TOPIC_UPTIME, (int)(millis() / 1000));
    }
}

void onConfig(MqttParam& param) {
    Serial.printf("[CONFIG] %zu bytes diterima\n", param.length());
}

void onWildcard(const char* payload, size_t length) {
    Serial.printf("[device/#] %.*s\n", (int)length, payload);
}

// ─── Connect & Disconnect callbacks ──────────────────────────────────────────

void onMqttConnected() {
    Serial.println("[MQTT] Tersambung ke broker!");
    Serial.printf("[MQTT] Topik $status: %s\n", mqtt.statusTopic());

    mqtt.subscribe(TOPIC_CMD,    onCmd,      QOS1);
    mqtt.subscribe(TOPIC_CONFIG, onConfig,   QOS1);
    mqtt.subscribe(TOPIC_ALL,    onWildcard, QOS0);
}

void onMqttDisconnected() {
    Serial.println("[MQTT] Terputus — library akan reconnect otomatis.");
}

// ─── Global fallback ──────────────────────────────────────────────────────────

void onMessage(const char* topic, const char* payload, size_t length) {
    Serial.printf("[MQTT] %s => %.*s\n", topic, (int)length, payload);
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n[ElectinsIoT] AdvancedIoT v2");

    mqtt.setDebug(true);
    mqtt.setKeepAlive(30);
    mqtt.onConnect(onMqttConnected);
    mqtt.onDisconnect(onMqttDisconnected);
    mqtt.onMessage(onMessage);

    mqtt.begin(
        WIFI_SSID,   WIFI_PASS,
        MQTT_HOST,   MQTT_PORT,
        "DeviceID-Advanced",
        MQTT_USER,   MQTT_PASS,
        USER_PREFIX, PROJECT_SLUG
    );
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    static uint32_t last = 0;
    if (millis() - last >= 10000) {
        last = millis();
        mqtt.publish(TOPIC_TEMP,   25.0f + random(0, 50) / 10.0f);
        mqtt.publish(TOPIC_UPTIME, (int)(millis() / 1000));
        mqtt << "username/myproject/hello:world";
    }
}
