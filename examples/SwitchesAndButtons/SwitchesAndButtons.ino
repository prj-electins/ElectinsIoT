/**
 * SwitchesAndButtons.ino — ElectinsIoT v3.0.3 Example
 * ─────────────────────────────────────────────────────────────────────────────
 * Contoh penggunaan widget saklar dan tombol kontrol:
 * - Switch        : Mengontrol status On/Off saklar atau relay
 * - Button        : Tombol pemicu sementara (momentary)
 * - Push Button   : Tombol tekan berstatus
 * - LED Indicator : Menampilkan indikator status perangkat pada aplikasi
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
const char* PARAM_RELAY1      = "relay1";         // Switch
const char* PARAM_RELAY2      = "relay2";         // Switch
const char* PARAM_BUZZER      = "trigger_buzzer"; // Button (Momentary)
const char* PARAM_POMPA       = "pompa_utama";    // Push Button
const char* PARAM_LED_STATUS  = "led_status";     // LED Indicator

// ─── Pin Perangkat Hardware ───────────────────────────────────────────────────
#define PIN_RELAY1 2
#define PIN_RELAY2 4
#define PIN_BUZZER 5

// ─── Inisialisasi Library ────────────────────────────────────────────────────
WiFiClient client;
ElectinsIoT iot(client);

void setup() {
    Serial.begin(115200);
    pinMode(PIN_RELAY1, OUTPUT);
    pinMode(PIN_RELAY2, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);

    digitalWrite(PIN_RELAY1, LOW);
    digitalWrite(PIN_RELAY2, LOW);
    digitalWrite(PIN_BUZZER, LOW);

    iot.setDebug(true);

    // Menerima dan merespons perintah kontrol dari aplikasi
    iot.onUpdateParam([](const char* param, double val, const char* strVal) {
        Serial.printf("Perintah Diterima -> %s: %.1f\n", param, val);

        // Kontrol Saklar Relay 1 & Relay 2
        if (strcmp(param, PARAM_RELAY1) == 0) {
            digitalWrite(PIN_RELAY1, val > 0.5 ? HIGH : LOW);
        } else if (strcmp(param, PARAM_RELAY2) == 0) {
            digitalWrite(PIN_RELAY2, val > 0.5 ? HIGH : LOW);
        }
        
        // Pemicu Tombol Sementara (Buzzer)
        else if (strcmp(param, PARAM_BUZZER) == 0 && val > 0.5) {
            digitalWrite(PIN_BUZZER, HIGH);
            delay(100);
            digitalWrite(PIN_BUZZER, LOW);
        }

        // Kontrol Tombol Pompa
        else if (strcmp(param, PARAM_POMPA) == 0) {
            Serial.printf("Status Pompa: %s\n", val > 0.5 ? "Aktif" : "Non-aktif");
        }

        // Memperbarui indikator status di aplikasi
        bool systemActive = (digitalRead(PIN_RELAY1) == HIGH || digitalRead(PIN_RELAY2) == HIGH);
        iot.sendTelemetryBool(PARAM_LED_STATUS, systemActive);
    });

    // Menghubungkan perangkat ke jaringan dan layanan
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER);
}

void loop() {
    // Proses komunikasi dan penerimaan perintah berjalan otomatis di latar belakang
}
