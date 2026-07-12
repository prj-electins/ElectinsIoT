/**
 * SecureIoT.ino — ElectinsIoT v3.0.1
 * ──────────────────────────────────────────────────────────
 * Contoh dasar penggunaan koneksi aman terenkripsi (SSL/TLS)
 * untuk mengontrol lampu dan mengirim data tekanan.
 */

#include <ElectinsIoT.h>
#include <WiFiClientSecure.h>

// ─── Kredensial WiFi, API Key, & Versi Firmware ──────────────────────────────
const char* WIFI_SSID    = "YOUR_WIFI_SSID";
const char* WIFI_PASS    = "YOUR_WIFI_PASSWORD";
const char* API_KEY      = "YOUR_API_KEY";
const char* FIRMWARE_VER = "1.0.0";

// Sertifikat Root CA Let's Encrypt (PEM) untuk iot.electins.id
const char* ROOT_CA_CERT = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPGu2OC+XYwDQYJKoZIhvcNAQELBQAw\n" \
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n" \
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n" \
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n" \
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n" \
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJzwa3fNWk5BIZOIO\n" \
"担当者v5GvFK1TUKcgJMcJ+KGn3H4C2O9C7vN7WwQ+7bM6z2H0GCIH0Z715e7uH7\n" \
"......\n" \
"-----END CERTIFICATE-----\n";

// ─── Parameter Global (Nama Widget Dashboard) ────────────────────────────────
const char* PARAM_LAMPU     = "lampu";
const char* PARAM_TEKANAN   = "tekanan";

// ─── Instansiasi Soket TLS Aman & ElectinsIoT ────────────────────────────────
WiFiClientSecure secureClient;
ElectinsIoT iot(secureClient);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[ElectinsIoT] SecureIoT v3.0.1 (TLS)");

    // ── Konfigurasi TLS Aman pada Klien Soket ────────────────────────────────
#if defined(ESP32)
    secureClient.setCACert(ROOT_CA_CERT);
#elif defined(ESP8266)
    secureClient.setTrustAnchors(new BearSSL::X509List(ROOT_CA_CERT));
#endif

    // Aktifkan log debug
    iot.setDebug(true);

    // Mulai inisialisasi otomatis Wi-Fi & TCP Server port aman 8883 (TLS).
    // Parameter kelima (true) mengaktifkan SSL/TLS otomatis pada port 8883.
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER, true);
}

void loop() {
    // 1. MEMBACA PARAMETER (GET) - Polling nilai "lampu" dari cache lokal (default: 0.0)
    double lampuState = iot.getDouble(PARAM_LAMPU, 0.0);
    
    // 2. TELEMETRI SENSOR (SET) - Mengirim data tekanan setiap 10 detik
    static unsigned long lastSend = 0;
    if (millis() - lastSend >= 10000) {
        lastSend = millis();
        if (iot.connected()) {
            float pressure = 10.0f + random(0, 100) / 10.0f;
            iot.sendTelemetry(PARAM_TEKANAN, pressure);
            Serial.printf("[Sensor] Tekanan: %.1f Pa | Status Lampu: %s\n", pressure, lampuState > 0.5 ? "ON" : "OFF");
        }
    }
}
