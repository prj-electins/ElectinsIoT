/**
 * SlidersAndSteppers.ino — ElectinsIoT v3.0.3 Example
 * ─────────────────────────────────────────────────────────────────────────────
 * Contoh penggunaan widget pengatur nilai bertahap:
 * - Slider (Linear) — Mengatur nilai bertahap atau kecerahan (0 - 255)
 * - Arc Slider      — Mengatur nilai melingkar atau persentase (0 - 100%)
 * - Stepper (H)     — Mengatur target nilai secara horizontal (+/-)
 * - Stepper (V)     — Mengatur batas nilai secara vertikal (+/-)
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
const char* PARAM_DIMMER       = "kecerahan_led"; // Linear Slider
const char* PARAM_ARC_SPEED    = "kecepatan_fan"; // Arc Slider
const char* PARAM_SETPOINT_H   = "target_temp";   // Stepper Horizontal
const char* PARAM_VOLUME_LIMIT = "volume_limit";  // Stepper Vertical

// ─── Variable Status Perangkat ───────────────────────────────────────────────
int ledBrightness = 128;
int fanSpeedPct   = 50;
float targetTemp  = 24.0;
int volumeLimit   = 80;

// ─── Inisialisasi Library ────────────────────────────────────────────────────
WiFiClient client;
ElectinsIoT iot(client);

void setup() {
    Serial.begin(115200);
    iot.setDebug(true);

    // Menerima penyesuaian nilai dari aplikasi
    iot.onUpdateParam([](const char* param, double val, const char* strVal) {
        // Penyesuaian Slider Kecerahan (0 - 255)
        if (strcmp(param, PARAM_DIMMER) == 0) {
            ledBrightness = (int)val;
            Serial.printf("Kecerahan LED: %d / 255\n", ledBrightness);
        }
        
        // Penyesuaian Arc Slider Kecepatan (0 - 100%)
        else if (strcmp(param, PARAM_ARC_SPEED) == 0) {
            fanSpeedPct = (int)val;
            Serial.printf("Kecepatan Kipas: %d%%\n", fanSpeedPct);
        }
        
        // Penyesuaian Target Suhu
        else if (strcmp(param, PARAM_SETPOINT_H) == 0) {
            targetTemp = val;
            Serial.printf("Target Suhu: %.1f°C\n", targetTemp);
        }

        // Penyesuaian Batas Volume
        else if (strcmp(param, PARAM_VOLUME_LIMIT) == 0) {
            volumeLimit = (int)val;
            Serial.printf("Batas Volume: %d L\n", volumeLimit);
        }
    });

    // Menghubungkan perangkat ke jaringan dan layanan
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER);
}

void loop() {
    // Proses komunikasi dan penerimaan perintah berjalan otomatis di latar belakang
}
