/**
 * FullAutomationMaster.ino — ElectinsIoT v3.0.3 Example
 * ─────────────────────────────────────────────────────────────────────────────
 * Contoh integrasi sistem dan kontrol perintah suara:
 * - Radial Progress : Menampilkan indikator persentase beban atau status
 * - Voice Control   : Memproses teks perintah suara untuk mengontrol perangkat
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
const char* PARAM_VOICE_CMD  = "perintah_suara"; // Voice Control
const char* PARAM_RADIAL_LOAD= "beban_sistem";   // Radial Progress Chart
const char* PARAM_LAMPU_UTAMA= "lampu_utama";    // Switch

// ─── Pin Perangkat Hardware ───────────────────────────────────────────────────
#define PIN_LAMPU 2

// ─── Inisialisasi Library ────────────────────────────────────────────────────
WiFiClient client;
ElectinsIoT iot(client);

void setup() {
    Serial.begin(115200);
    pinMode(PIN_LAMPU, OUTPUT);
    digitalWrite(PIN_LAMPU, LOW);

    iot.setDebug(true);

    // Menerima perintah kontrol (termasuk hasil suara dari aplikasi)
    iot.onUpdateParam([](const char* param, double val, const char* strVal) {
        // Memproses Perintah Suara
        if (strcmp(param, PARAM_VOICE_CMD) == 0 && strVal != nullptr) {
            String voiceCmd = String(strVal);
            voiceCmd.toLowerCase();

            Serial.printf("Perintah Suara Diterima: '%s'\n", strVal);

            if (voiceCmd.indexOf("nyalakan lampu") >= 0 || voiceCmd.indexOf("lampu on") >= 0) {
                digitalWrite(PIN_LAMPU, HIGH);
                iot.sendTelemetryBool(PARAM_LAMPU_UTAMA, true);
                Serial.println("-> Lampu Dinyalakan");
            } 
            else if (voiceCmd.indexOf("matikan lampu") >= 0 || voiceCmd.indexOf("lampu off") >= 0) {
                digitalWrite(PIN_LAMPU, LOW);
                iot.sendTelemetryBool(PARAM_LAMPU_UTAMA, false);
                Serial.println("-> Lampu Dimatikan");
            }
        }

        // Memproses Kontrol Saklar Langsung
        else if (strcmp(param, PARAM_LAMPU_UTAMA) == 0) {
            digitalWrite(PIN_LAMPU, val > 0.5 ? HIGH : LOW);
        }
    });

    // Menghubungkan perangkat ke jaringan dan layanan
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER);
}

void loop() {
    // Memperbarui persentase indikator beban sistem pada aplikasi secara berkala
    static unsigned long lastSend = 0;
    if (millis() - lastSend >= 5000) {
        lastSend = millis();

        if (iot.connected()) {
            float sysLoad = 45.0 + random(-15, 15);
            if (sysLoad < 0) sysLoad = 0;
            if (sysLoad > 100) sysLoad = 100;

            iot.sendTelemetry(PARAM_RADIAL_LOAD, sysLoad);
            Serial.printf("Beban Sistem: %.1f%%\n", sysLoad);
        }
    }
}
