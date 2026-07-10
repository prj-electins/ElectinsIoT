/**
 * BasicIoT.ino — ElectinsIoT v3.0.0
 * ─────────────────────────────────────────────
 * Contoh dasar penggunaan pustaka ElectinsIoT
 * untuk mengontrol lampu dan mengirim data suhu.
 */

#include <ElectinsIoT.h>

// ─── Kredensial WiFi, API Key, & Versi Firmware ──────────────────────────────
const char* WIFI_SSID    = "YOUR_WIFI_SSID";
const char* WIFI_PASS    = "YOUR_WIFI_PASSWORD";
const char* API_KEY      = "YOUR_API_KEY";
const char* FIRMWARE_VER = "1.0.0";

// ─── Parameter Global (Nama Widget Dashboard) ────────────────────────────────
const char* PARAM_LED    = "led";
const char* PARAM_SUHU   = "suhu";

// ─── Instansiasi Soket & Library ─────────────────────────────────────────────
WiFiClient client;
ElectinsIoT iot(client);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n[ElectinsIoT] BasicIoT v%s\n", FIRMWARE_VER);

    // Aktifkan log debug pustaka
    iot.setDebug(true);

    // Inisialisasi otomatis Wi-Fi & TCP Server.
    // Pustaka akan menangani koneksi, rekoneksi, ping, dan OTA secara latar belakang.
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER);

    // Set LED bawaan board sebagai OUTPUT
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    // MEMBACA PARAMETER (GET) - Polling nilai dari cache lokal menggunakan variabel global
    bool ledState = iot.getBool(PARAM_LED, false);
    if (ledState) {
        digitalWrite(LED_BUILTIN, HIGH);
    } else {
        digitalWrite(LED_BUILTIN, LOW);
    }

    // MENGIRIM DATA (SET/TELEMETRI) - Mengirim data suhu setiap 3 detik
    static unsigned long lastSend = 0;
    if (millis() - lastSend >= 3000) {
        lastSend = millis();
        if (iot.connected()) {
            float temperature = 25.0f + random(0, 50) / 10.0f;
            iot.sendTelemetry(PARAM_SUHU, temperature);
        }
    }
}
