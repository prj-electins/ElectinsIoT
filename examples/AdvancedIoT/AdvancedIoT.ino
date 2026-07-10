/**
 * AdvancedIoT.ino — ElectinsIoT v3.0.0 Advanced Features
 * ──────────────────────────────────────────────────
 * Mendemonstrasikan kontrol parameter multi-tipe (downlink) asinkron lewat cache (GET),
 * pengiriman data batch hemat kuota (SET), serta pendaftaran callback reboot aman.
 * Pustaka otomatis mengurus Wi-Fi, TCP, heartbeat ping, & OTA update di latar belakang.
 */

#include <ElectinsIoT.h>

// ─── Kredensial WiFi, API Key, & Versi Firmware ──────────────────────────────
const char* WIFI_SSID    = "YOUR_WIFI_SSID";
const char* WIFI_PASS    = "YOUR_WIFI_PASSWORD";
const char* API_KEY      = "YOUR_API_KEY";
const char* FIRMWARE_VER = "1.0.0";

// ─── Parameter Global (Nama Widget Dashboard) ────────────────────────────────
const char* PARAM_LED           = "led";
const char* PARAM_RELAY         = "relay";
const char* PARAM_SUHU          = "suhu";
const char* PARAM_UPTIME        = "uptime";
const char* PARAM_STATUS_KABEL  = "status_kabel";
const char* PARAM_MODE           = "mode";
const char* PARAM_MODE_AKTIF     = "mode_aktif";

// ─── Instansiasi Soket & Library ─────────────────────────────────────────────
WiFiClient client;
ElectinsIoT iot(client);

// ─── Callback reboot nirkabel (opsional, dipanggil sesaat sebelum reboot) ────
void onReboot() {
    Serial.println("[CMD] Mendapatkan perintah restart, membersihkan hardware...");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n[ElectinsIoT] AdvancedIoT v%s\n", FIRMWARE_VER);

    // Aktifkan log debug pustaka
    iot.setDebug(true);

    // Daftarkan callback reboot aman (opsional)
    iot.onReboot(onReboot);

    // Mulai inisialisasi otomatis Wi-Fi & TCP Server.
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER);
}

void loop() {
    // 1. MEMBACA PARAMETER (GET) - Polling nilai dari cache lokal
    double ledState = iot.getDouble(PARAM_LED, 0.0);
    double relayState = iot.getDouble(PARAM_RELAY, 0.0);
    const char* deviceMode = iot.getString(PARAM_MODE, "normal");

    // 2. TELEMETRI BATCH (SET) - Mengirim multi-parameter sekaligus setiap 10 detik
    static unsigned long lastSend = 0;
    if (millis() - lastSend >= 10000) {
        lastSend = millis();
        
        if (iot.connected()) {
            float dummyTemp = 24.0f + random(0, 40) / 10.0f;
            float uptimeSeconds = millis() / 1000.0f;
            
            // Pengiriman Batch
            iot.startBatch();
            iot.addBatch(PARAM_SUHU, dummyTemp);
            iot.addBatch(PARAM_UPTIME, uptimeSeconds);
            iot.addBatchString(PARAM_STATUS_KABEL, "koneksi_baik");
            iot.addBatchString(PARAM_MODE_AKTIF, deviceMode);
            iot.sendBatch();
            
            Serial.printf("[Telemetry] Batch Sent. LED: %.1f | Relay: %.1f | Mode: %s\n", 
                          ledState, relayState, deviceMode);
        }
    }
}
