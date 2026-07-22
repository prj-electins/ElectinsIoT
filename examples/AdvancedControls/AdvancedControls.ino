/**
 * AdvancedControls.ino — ElectinsIoT v3.0.3 Example
 * ─────────────────────────────────────────────────────────────────────────────
 * Contoh penggunaan widget interaktif tambahan:
 * - Color Picker — Memilih warna lampu RGB
 * - GPS Map      — Menampilkan lokasi koordinat perangkat
 * - Text Input   — Mengirimkan pesan teks atau tulisan
 * - Time Picker  — Menentukan jadwal waktu operasional
 *
 * Board: ESP32 / ESP8266
 */

#include <ElectinsIoT.h>

// ─── Konfigurasi Perangkat ───────────────────────────────────────────────────
const char* WIFI_SSID    = "YOUR_WIFI_SSID";
const char* WIFI_PASS    = "YOUR_WIFI_PASSWORD";
const char* API_KEY      = "YOUR_API_KEY";
const char* FIRMWARE_VER = "1.0.0";

// ─── Nama Parameter Widget ───────────────────────────────────────────────────
const char* PARAM_RGB_COLOR   = "warna_led";    // Color Picker
const char* PARAM_GPS_MAP     = "lokasi_gps";   // GPS Map
const char* PARAM_RUNNING_TXT = "running_text"; // Text Input
const char* PARAM_SCHEDULE_TIME= "jam_nyala";   // Time Picker

// ─── Inisialisasi Library ────────────────────────────────────────────────────
WiFiClient client;
ElectinsIoT iot(client);

void setup() {
    Serial.begin(115200);
    iot.setDebug(true);

    // Menerima data warna dan teks dari aplikasi
    iot.onUpdateParam([](const char* param, double val, const char* strVal) {
        // Penerimaan Warna RGB (Format Kode Warna)
        if (strcmp(param, PARAM_RGB_COLOR) == 0 && strVal != nullptr) {
            Serial.printf("Warna Diterima: %s\n", strVal);
        }

        // Penerimaan Teks Pesan
        else if (strcmp(param, PARAM_RUNNING_TXT) == 0 && strVal != nullptr) {
            Serial.printf("Pesan Teks Diterima: '%s'\n", strVal);
        }

        // Penerimaan Waktu Operasional (Format Jam:Menit)
        else if (strcmp(param, PARAM_SCHEDULE_TIME) == 0 && strVal != nullptr) {
            Serial.printf("Waktu Operasional: %s\n", strVal);
        }
    });

    // Menghubungkan perangkat ke jaringan dan layanan
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER);
}

void loop() {
    // Memperbarui lokasi koordinat perangkat pada aplikasi secara berkala
    static unsigned long lastGpsSend = 0;
    if (millis() - lastGpsSend >= 10000) {
        lastGpsSend = millis();

        if (iot.connected()) {
            // Format Koordinat Lokasi: "latitude,longitude"
            const char* currentGps = "-6.2088,106.8456";
            iot.sendTelemetryString(PARAM_GPS_MAP, currentGps);
            Serial.printf("Koordinat Lokasi: %s\n", currentGps);
        }
    }
}
