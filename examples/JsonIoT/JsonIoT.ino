/**
 * JsonIoT.ino — ElectinsIoT v3.0.2 JSON Serialization Example
 * ──────────────────────────────────────────────────────────
 * Demonstrasi pengiriman data terformat JSON dan pemrosesan 
 * konfigurasi JSON yang diterima dari server menggunakan ArduinoJson.
 *
 * Dependensi: ArduinoJson by Benoit Blanchon (install via Library Manager)
 * TCP/Protobuf: built-in SDK ESP32/ESP8266
 */

#include <ArduinoJson.h>
#include <ElectinsIoT.h>

// ─── Konfigurasi WiFi & API Key ──────────────────────────────────────────────
const char* WIFI_SSID   = "YOUR_WIFI_SSID";
const char* WIFI_PASS   = "YOUR_WIFI_PASSWORD";
const char* API_KEY     = "YOUR_API_KEY";

// ─── Parameter Global (Dashboard Widgets) ─────────────────────────────────────
const char* PARAM_KONFIGURASI   = "konfigurasi";
const char* PARAM_SENSOR_LOG    = "sensor_log";

// ─── Instansiasi Soket & Pustaka ─────────────────────────────────────────────
WiFiClient client;
ElectinsIoT iot(client);

// ─── Callback perintah masuk ──────────────────────────────────────────────────
// Menangani parameter 'konfigurasi' yang berisi string JSON dari server
void onUpdateParam(const char* param, double value, const char* stringValue) {
    if (strcmp(param, PARAM_KONFIGURASI) == 0) {
        Serial.printf("[CONFIG] Menerima JSON untuk %s:\n", PARAM_KONFIGURASI);
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, stringValue);
        
        if (error) {
            Serial.print("  Gagal mendeserialisasi JSON: ");
            Serial.println(error.c_str());
            return;
        }
        
        // Baca field dari JSON
        int interval = doc["interval"] | 10000;
        bool modeHemat = doc["mode_hemat"] | false;
        
        Serial.printf("  interval   = %d ms\n", interval);
        Serial.printf("  mode_hemat = %s\n", modeHemat ? "aktif" : "nonaktif");
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n[ElectinsIoT] JsonIoT v3 Redesign");

    // Hubungkan WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    Serial.println("WiFi Connected!");

    iot.setDebug(true);
    iot.onUpdateParam(onUpdateParam);

    iot.begin(API_KEY);
    iot.connect("iot.electins.id", 1883);
}

void loop() {
    if (!iot.connected()) {
        static unsigned long lastReconnect = 0;
        if (millis() - lastReconnect > 5000) {
            lastReconnect = millis();
            iot.connect("iot.electins.id", 1883);
        }
    }
    iot.loop();

    static uint32_t last = 0;
    if (millis() - last >= 10000) {
        last = millis();

        if (iot.connected()) {
            // 1. Buat object JSON
            JsonDocument doc;
            doc["suhu"] = 25.0f + random(0, 50) / 10.0f;
            doc["kelembapan"] = 60.0f + random(0, 200) / 10.0f;
            doc["uptime"] = millis() / 1000;

            // 2. Serialisasikan ke string buffer
            char jsonBuffer[256];
            serializeJson(doc, jsonBuffer);

            // 3. Kirim string JSON sebagai satu parameter telemetri teks
            if (iot.sendTelemetryString(PARAM_SENSOR_LOG, jsonBuffer)) {
                Serial.printf("[Sensor] Mengirim JSON via %s: %s\n", PARAM_SENSOR_LOG, jsonBuffer);
            }
        }
    }
}
