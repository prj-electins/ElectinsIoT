/**
 * RelayDHT.ino — ElectinsIoT v3.0.3 Relay + DHT Sensor
 * ──────────────────────────────────────────────────
 * Kontrol relay dan kirim data sensor DHT11/DHT22 secara otomatis latar belakang.
 * Pustaka otomatis mengurus Wi-Fi, TCP, heartbeat ping, & OTA update.
 *
 * Dependensi: DHT sensor library by Adafruit
 */

#include <ElectinsIoT.h>
#include <DHT.h>

// ─── Kredensial WiFi, API Key, & Versi Firmware ──────────────────────────────
const char* WIFI_SSID    = "YOUR_WIFI_SSID";
const char* WIFI_PASS    = "YOUR_WIFI_PASSWORD";
const char* API_KEY      = "YOUR_API_KEY";
const char* FIRMWARE_VER = "1.0.0";

// ─── Parameter Global (Nama Widget Dashboard) ────────────────────────────────
const char* PARAM_RELAY       = "relay";
const char* PARAM_RELAY_STATE = "relay_state";
const char* PARAM_SUHU        = "suhu";
const char* PARAM_KELEMBAPAN  = "kelembapan";

// ─── Hardware Pin ─────────────────────────────────────────────────────────────
#define PIN_DHT   5
#define PIN_RELAY 2
DHT dht(PIN_DHT, DHT11);

// ─── Instansiasi Soket & Pustaka ─────────────────────────────────────────────
WiFiClient client;
ElectinsIoT iot(client);

void setup() {
    Serial.begin(115200);
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);
    dht.begin();

    // Aktifkan log debug
    iot.setDebug(true);

    // Registrasi callback untuk merespons status parameter dari server secara instan (event-driven)
    // Ini adalah cara terbaik agar status awal ter-load secara instan saat menyala/reconnect.
    iot.onUpdateParam([](const char* param, double val, const char* strVal) {
        if (strcmp(param, PARAM_RELAY) == 0) {
            digitalWrite(PIN_RELAY, val > 0.5 ? HIGH : LOW);
        }
    });

    // Mulai inisialisasi otomatis Wi-Fi & TCP Server.
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER);
}

void loop() {
    // TELEMETRI SENSOR (SET) - Kirim data suhu & kelembapan setiap 5 detik
    static unsigned long lastSend = 0;
    if (millis() - lastSend >= 5000) {
        lastSend = millis();
        
        float temp = dht.readTemperature();
        float hum  = dht.readHumidity();
        
        if (!isnan(temp) && !isnan(hum) && iot.connected()) {
            // Gunakan batching agar terkirim dalam 1 paket hemat data
            iot.startBatch();
            iot.addBatch(PARAM_SUHU, temp);
            iot.addBatch(PARAM_KELEMBAPAN, hum);
            iot.addBatch(PARAM_RELAY_STATE, digitalRead(PIN_RELAY) == HIGH ? 1.0 : 0.0); // Laporkan status relay balik ke server
            iot.sendBatch();
            
            Serial.printf("[Sensor] Suhu: %.1f°C | Kelembapan: %.1f%%\n", temp, hum);
        }
    }
}
