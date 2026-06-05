/**
 * SecureIoT.ino — ElectinsIoT v2.1.1 MQTT over TLS (port 8883)
 * ──────────────────────────────────────────────────────────
 * Koneksi MQTT terenkripsi TLS menggunakan WiFiClientSecure
 * Konfigurasi TLS HARUS dilakukan SEBELUM mqtt.begin().
 * Dependensi: built-in SDK ESP32/ESP8266
 */

#include <ElectinsIoT.h>

// ─── Konfigurasi ──────────────────────────────────────────────────────────────
const char*    WIFI_SSID    = "WIFI_SSID";
const char*    WIFI_PASS    = "WIFI_PASSWORD";
const char*    MQTT_HOST    = "iot.electins.id";
const char*    MQTT_USER    = "PRJ-XXXXXXXX";
const char*    MQTT_PASS    = "PASSWORD";
const char*    PROJECT_SLUG = "myproject";
const uint16_t MQTT_PORT    = 8883;           // Port MQTT over TLS

// ─── Topik ────────────────────────────────────────────────────────────────────
const char* TOPIC_CMD  = "username/myproject/cmd";
const char* TOPIC_TEMP = "username/myproject/temp";

ElectinsIoT mqtt;

// ─── Callbacks ────────────────────────────────────────────────────────────────
void onCmd(MqttParam& param) {
    Serial.printf("[CMD] %s\n", param.asStr());
}

void onMqttConnected() {
    Serial.println("[MQTT] Koneksi TLS berhasil!");
    Serial.printf("[MQTT] Topik $status: %s\n", mqtt.statusTopic());
    mqtt.subscribe(TOPIC_CMD, onCmd, QOS1);
}

void onMqttDisconnected() {
    Serial.println("[MQTT] Terputus.");
}

void onMessage(const char* topic, const char* payload, size_t length) {
    Serial.printf("[MQTT] %s => %.*s\n", topic, (int)length, payload);
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n[ElectinsIoT] SecureIoT v2");

    mqtt.setDebug(true);
    mqtt.onConnect(onMqttConnected);
    mqtt.onDisconnect(onMqttDisconnected);
    mqtt.onMessage(onMessage);

    // ── Aktifkan TLS — HARUS sebelum begin() ────────────────────────────────
    mqtt.setSecure(true);

    // Pilih salah satu:
    // skip verifikasi sertifikat (development / self-signed)
    mqtt.setInsecure(true);

    mqtt.begin(
        WIFI_SSID,   WIFI_PASS,
        MQTT_HOST,   MQTT_PORT,
        "DeviceID-Secure",
        MQTT_USER,   MQTT_PASS,
        PROJECT_SLUG
    );
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    static uint32_t last = 0;
    if (millis() - last >= 10000) {
        last = millis();
        mqtt.publish(TOPIC_TEMP, 25.0f + random(0, 50) / 10.0f);
    }
}
