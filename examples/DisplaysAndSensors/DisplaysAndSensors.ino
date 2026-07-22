/**
 * DisplaysAndSensors.ino — ElectinsIoT v3.0.3 Example
 * ─────────────────────────────────────────────────────────────────────────────
 * Contoh penggunaan widget untuk pemantauan data sensor:
 * - Value Display  : Menampilkan angka sensor
 * - Gauge Meter    : Menampilkan meteran analog
 * - Line Chart     : Menampilkan grafik riwayat data
 * - Label          : Menampilkan status teks
 * - Level Bars     : Menampilkan indikator level persentase
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
const char* PARAM_SUHU        = "suhu";          // Value Display & Line Chart
const char* PARAM_KELEMBAPAN  = "kelembapan";    // Value Display
const char* PARAM_TEKANAN     = "tekanan";       // Gauge Meter
const char* PARAM_BATERAI     = "baterai";       // Horizontal Level Bar
const char* PARAM_TANGKI      = "tangki_air";    // Vertical Level Bar
const char* PARAM_STATUS      = "status_sistem"; // Label

// ─── Inisialisasi Library ────────────────────────────────────────────────────
WiFiClient client;
ElectinsIoT iot(client);

void setup() {
    Serial.begin(115200);
    iot.setDebug(true);

    // Menghubungkan perangkat ke jaringan dan layanan
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER);
}

void loop() {
    // Membaca dan mengirimkan data sensor secara berkala (setiap 3 detik)
    static unsigned long lastSend = 0;
    if (millis() - lastSend >= 3000) {
        lastSend = millis();

        if (iot.connected()) {
            // Membaca data sensor
            float suhu       = 26.5 + (random(-10, 10) / 10.0);
            float kelembapan = 60.0 + random(-5, 5);
            float tekanan    = 75.0 + (random(-20, 20) / 10.0);
            float baterai    = 95.0 - (millis() / 60000.0);
            float tangkiAir  = 82.0 + (random(-10, 10) / 10.0);

            if (baterai < 10.0) baterai = 100.0;

            // Mengirimkan data sensor ke aplikasi
            iot.startBatch();
            iot.addBatch(PARAM_SUHU, suhu);
            iot.addBatch(PARAM_KELEMBAPAN, kelembapan);
            iot.addBatch(PARAM_TEKANAN, tekanan);
            iot.addBatch(PARAM_BATERAI, baterai);
            iot.addBatch(PARAM_TANGKI, tangkiAir);
            iot.addBatchString(PARAM_STATUS, "Sistem Berjalan Normal");
            iot.sendBatch();

            Serial.printf("Suhu: %.1f°C | Kelembapan: %.0f%% | Tekanan: %.1f | Baterai: %.0f%%\n",
                          suhu, kelembapan, tekanan, baterai);
        }
    }
}
