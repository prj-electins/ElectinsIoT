/**
 * BasicIoT.ino — ElectinsIoT v2.1.1 Basic Example
 * ─────────────────────────────────────────────
 * Library otomatis menangani di background:
 *   - Koneksi WiFi & MQTT
 *   - Heartbeat "online" setiap 30 detik via Ticker
 *   - Auto-reconnect WiFi & MQTT via event handler
 *
 * Dependensi: built-in SDK ESP32/ESP8266
 */

#include <ElectinsIoT.h>

// ─── Konfigurasi ──────────────────────────────────────────────────────────────
const char* WIFI_SSID    = "WIFI_SSID";
const char* WIFI_PASS    = "WIFI_PASSWORD";
const char* MQTT_HOST    = "iot.electins.id";
const char* MQTT_USER    = "PRJ-XXXXXXXX";
const char* MQTT_PASS    = "PASSWORD";
const char* PROJECT_SLUG = "myproject";
const uint16_t MQTT_PORT = 1883;

// ─── Topik ────────────────────────────────────────────────────────────────────
const char* TOPIC_CMD  = "username/myproject/cmd";
const char* TOPIC_TEMP = "username/myproject/temp";

// ─── Instance library ─────────────────────────────────────────────────────────
ElectinsIoT mqtt;

// ─── Callback perintah masuk ──────────────────────────────────────────────────
void onCmd(MqttParam& param) {
    Serial.printf("[CMD] Diterima: %s\n", param.asStr());
}

// ─── Callback saat MQTT tersambung (atau reconnect) ───────────────────────────
void onMqttConnected() {
    // Subscribe di sini — otomatis diulang saat reconnect
    mqtt.subscribe(TOPIC_CMD, onCmd);
    Serial.println("[MQTT] Tersambung dan siap.");
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n[ElectinsIoT] BasicIoT v2");

    mqtt.setDebug(true);
    mqtt.onConnect(onMqttConnected);

    mqtt.begin(
        WIFI_SSID,   WIFI_PASS,
        MQTT_HOST,   MQTT_PORT,
        "DeviceID-Basic",
        MQTT_USER,   MQTT_PASS,
        PROJECT_SLUG
    );
}

// ─── Loop  ────────────────────────────────────────────────────────────────────
void loop() {
    // Kirim data sensor setiap 10 detik
    static uint32_t last = 0;
    if (millis() - last >= 10000) {
        last = millis();
        float temp = 25.0f + random(0, 50) / 10.0f;
        mqtt.publish(TOPIC_TEMP, temp);
        Serial.printf("[Sensor] Temp: %.1f°C\n", temp);
    }

    // Tambahkan logika Anda di sini

