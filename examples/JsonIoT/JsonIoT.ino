/**
 * JsonIoT.ino — ElectinsIoT v2.1.4 JSON Helper Example
 * ──────────────────────────────────────────────────
 * Requires: ArduinoJson by Benoit Blanchon
 *           (install via Library Manager)
 *
 * PENTING: ArduinoJson HARUS di-include SEBELUM ElectinsIoT
 *          agar JSON helper diaktifkan via preprocessor.
 *
 * Demonstrasi:
 *   - subscribeJson() : payload JSON di-parse otomatis ke JsonDocument
 *   - publishJson()   : serialize JsonDocument & publish
 *
 * Dependensi: ArduinoJson by Benoit Blanchon (install via Library Manager)
 * TCP/MQTT: built-in SDK ESP32/ESP8266 — tidak perlu install library lain
 */

// ArduinoJson HARUS di-include SEBELUM ElectinsIoT
#include <ArduinoJson.h>
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
const char* TOPIC_SENSOR  = "ID-XXXXXXXX/project-slug/sensor";
const char* TOPIC_CONFIG  = "ID-XXXXXXXX/project-slug/config";
const char* TOPIC_COMMAND = "ID-XXXXXXXX/project-slug/command";

ElectinsIoT mqtt;

// ─── JSON subscribe callbacks ─────────────────────────────────────────────────

// Terima: {"interval":5000,"debug":true,"name":"sensor-01"}
void onConfig(const char* topic, JsonDocument& doc) {
    Serial.println("[CONFIG] Diterima:");
    if (doc["interval"].is<int>())
        Serial.printf("  interval = %d ms\n", (int)doc["interval"]);
    if (doc["debug"].is<bool>())
        mqtt.setDebug((bool)doc["debug"]);
    if (doc["name"].is<const char*>())
        Serial.printf("  name     = %s\n", doc["name"].as<const char*>());
}

// Terima: {"action":"restart"} atau {"action":"status"}
void onCommand(const char* topic, JsonDocument& doc) {
    const char* action = doc["action"] | "";
    Serial.printf("[CMD] action = %s\n", action);

    if (strcmp(action, "restart") == 0) {
        delay(300);
        ESP.restart();
    }

    if (strcmp(action, "status") == 0) {
        JsonDocument reply;
        reply["uptime"] = millis() / 1000;
        reply["heap"]   = ESP.getFreeHeap();
        reply["rssi"]   = WiFi.RSSI();
        mqtt.publishJson("username/myproject/status/reply", reply);
    }
}

// ─── Connect callback ─────────────────────────────────────────────────────────
void onMqttConnected() {
    Serial.println("[MQTT] Tersambung!");
    Serial.printf("[MQTT] Topik $status: %s\n", mqtt.statusTopic());

    mqtt.subscribeJson(TOPIC_CONFIG,  onConfig,  QOS1);
    mqtt.subscribeJson(TOPIC_COMMAND, onCommand, QOS1);
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n[ElectinsIoT] JsonIoT v2");

    mqtt.onConnect(onMqttConnected);

    mqtt.begin(
        WIFI_SSID,   WIFI_PASS,
        MQTT_HOST,   MQTT_PORT,
        "DeviceID-JSON",
        MQTT_USER,   MQTT_PASS,
        USER_PREFIX, PROJECT_SLUG
    );
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    static uint32_t last = 0;
    if (millis() - last >= 10000) {
        last = millis();

        JsonDocument sensor;
        sensor["temp"]   = 25.0f + random(0, 50) / 10.0f;
        sensor["hum"]    = 60.0f + random(0, 200) / 10.0f;
        sensor["uptime"] = millis() / 1000;

        if (mqtt.publishJson(TOPIC_SENSOR, sensor))
            Serial.println("[Sensor] JSON dipublish");
    }
}
