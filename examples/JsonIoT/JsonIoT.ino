/**
 * JsonIoT.ino — ElectinsIoT JSON Helper Example
 *
 * Requires: ArduinoJson library (https://arduinojson.org)
 * Install via Arduino Library Manager: search "ArduinoJson" by Benoit Blanchon
 *
 * Demonstrates:
 *  - publishJson()   : serialize and publish a JsonDocument as MQTT payload
 *  - subscribeJson() : automatically parse incoming JSON payload into JsonDocument
 */

// ArduinoJson MUST be included BEFORE ElectinsIoT to enable JSON helpers
#include <ArduinoJson.h>
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
const char* TOPIC_SENSOR  = "device/sensor";   // outgoing: sensor data as JSON
const char* TOPIC_CONFIG  = "device/config";   // incoming: device configuration
const char* TOPIC_COMMAND = "device/command";  // incoming: remote commands

WiFiClient wifiClient;
ElectinsIoT mqtt(wifiClient);

// ─── JSON subscribe handlers ──────────────────────────────────────────────────

// Receive device configuration from broker
// Example payload: {"interval":5000,"debug":true,"name":"sensor-01"}
void onConfig(const char* topic, JsonDocument& doc) {
    Serial.println("[CONFIG] Received:");

    if (doc["interval"].is<int>()) {
        int interval = doc["interval"];
        Serial.printf("  interval = %d ms\n", interval);
    }

    if (doc["debug"].is<bool>()) {
        bool dbg = doc["debug"];
        mqtt.setDebug(dbg);
        Serial.printf("  debug    = %s\n", dbg ? "true" : "false");
    }

    if (doc["name"].is<const char*>()) {
        Serial.printf("  name     = %s\n", doc["name"].as<const char*>());
    }
}

// Receive remote commands from broker
// Example payload: {"action":"restart"} or {"action":"status"}
void onCommand(const char* topic, JsonDocument& doc) {
    const char* action = doc["action"] | "";
    Serial.printf("[CMD] action = %s\n", action);

    if (strcmp(action, "restart") == 0) {
        Serial.println("[CMD] Restarting...");
        delay(500);
        ESP.restart();
    }

    if (strcmp(action, "status") == 0) {
        // Reply with current device status as JSON
        JsonDocument reply;
        reply["uptime"] = millis() / 1000;
        reply["heap"]   = ESP.getFreeHeap();
        reply["rssi"]   = WiFi.RSSI();
        mqtt.publishJson("device/status/reply", reply);
    }
}

// ─── Connect callback ─────────────────────────────────────────────────────────
void onConnected() {
    Serial.println("[MQTT] Connected!");

    // Publish online status as JSON with retain flag
    JsonDocument status;
    status["state"] = "online";
    status["ip"]    = WiFi.localIP().toString();
    mqtt.publishJson(TOPIC_STATUS, status, true);

    // Subscribe to config and command topics with JSON callbacks
    mqtt.subscribeJson(TOPIC_CONFIG,  onConfig,  QOS1);
    mqtt.subscribeJson(TOPIC_COMMAND, onCommand, QOS1);

    Serial.println("[MQTT] Subscribed to config & command topics");
}

void onDisconnected() {
    Serial.println("[MQTT] Disconnected!");
}

// ─── WiFi ─────────────────────────────────────────────────────────────────────
void reconnectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500);
    }
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n[ElectinsIoT] JSON Example");

    // LWT as a JSON string — setWill only accepts plain strings
    mqtt.setWill(TOPIC_STATUS, "{\"state\":\"offline\"}", true);
    mqtt.onConnect(onConnected);
    mqtt.onDisconnect(onDisconnected);

    mqtt.begin(WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, "DeviceID-JSON", MQTT_USER, MQTT_PASS);
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    reconnectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;

    mqtt.run();

    // Publish sensor data as JSON every 10 seconds
    static uint32_t last = 0;
    if (millis() - last >= 10000) {
        last = millis();

        JsonDocument sensor;
        sensor["temp"]   = 25.0f + random(0, 50) / 10.0f;
        sensor["hum"]    = 60.0f + random(0, 200) / 10.0f;
        sensor["uptime"] = millis() / 1000;

        if (mqtt.publishJson(TOPIC_SENSOR, sensor)) {
            Serial.println("[SENSOR] JSON published");
        }
    }
}
