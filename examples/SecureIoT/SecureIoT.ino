#include <ElectinsIoT.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiClientSecureBearSSL.h>
  using SecureClient = BearSSL::WiFiClientSecure;
#elif defined(ESP32)
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  using SecureClient = WiFiClientSecure;
#endif

// ─── Configuration ────────────────────────────────────────────────────────────
const char* WIFI_SSID  = "YourSSID";
const char* WIFI_PASS  = "YourPassword";
const char* MQTT_HOST  = "broker.example.com";
const char* MQTT_USER  = "username";
const char* MQTT_PASS  = "password";
const uint16_t MQTT_PORT = 8883;

// ─── Topics ───────────────────────────────────────────────────────────────────
const char* TOPIC_STATUS = "device/status";
const char* TOPIC_CMD    = "device/cmd";
const char* TOPIC_TEMP   = "device/temp";

SecureClient secureClient;
ElectinsIoT mqtt(secureClient);

// ─── Per-topic handler ────────────────────────────────────────────────────────
void onCmd(MqttParam& param) {
    Serial.printf("[CMD] %s\n", param.asStr());
}

// ─── Connect callback ─────────────────────────────────────────────────────────
void onConnected() {
    Serial.println("[MQTT] Secure connected!");
    mqtt.publish(TOPIC_STATUS, "online", true);
    mqtt.subscribe(TOPIC_CMD, onCmd, QOS1);
    Serial.print("[MQTT] Subscribed: "); Serial.println(TOPIC_CMD);
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

    // IMPORTANT: setInsecure() must be called BEFORE begin()
    // so TLS is configured before the TCP connection is established
    secureClient.setInsecure();

    mqtt.setWill(TOPIC_STATUS, "offline", true);
    mqtt.setDebug(true);
    mqtt.onConnect(onConnected);
    mqtt.onDisconnect(onDisconnected);
    mqtt.onMessage(onMessage);

    mqtt.begin(WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, "DeviceID-Secure", MQTT_USER, MQTT_PASS);
}

void loop() {
    reconnectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;

    mqtt.run();

    // Publish temperature every 10 seconds
    static uint32_t last = 0;
    if (millis() - last >= 10000) {
        last = millis();
        mqtt.publish(TOPIC_TEMP, 25.0f + random(0, 50) / 10.0f);
    }
}
