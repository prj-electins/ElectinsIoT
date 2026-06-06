/**
 * RelayDHT.ino — ElectinsIoT v2.1.3 Relay + DHT Sensor
 * ──────────────────────────────────────────────────
 * Kontrol relay via MQTT dan kirim data sensor DHT11/DHT22.
 *
 * Dependensi: DHT sensor library by Adafruit
 * TCP/MQTT: built-in SDK ESP32/ESP8266
 */

#include <ElectinsIoT.h>
#include <DHT.h>

// ─── Konfigurasi ──────────────────────────────────────────────────────────────
const char*    WIFI_SSID    = "WIFI_SSID";
const char*    WIFI_PASS    = "WIFI_PASSWORD";
const char*    MQTT_HOST    = "iot.electins.id";
const char*    MQTT_USER    = "PRJ-XXXXXXXX";
const char*    MQTT_PASS    = "PASSWORD";
const char*    USER_PREFIX  = "ID-XXXXXXXX"; 
const char*    PROJECT_SLUG = "project-slug";
const uint16_t MQTT_PORT    = 1883;

// ─── Topik ────────────────────────────────────────────────────────────────────
const char* TOPIC_TEMP        = "ID-XXXXXXXX/project-slug/temp";
const char* TOPIC_HUMIDITY    = "ID-XXXXXXXX/project-slug/humd";
const char* TOPIC_RELAY       = "ID-XXXXXXXX/project-slug/relay";
const char* TOPIC_RELAY_STATE = "ID-XXXXXXXX/project-slug/relay-state";

// ─── Hardware ─────────────────────────────────────────────────────────────────
#define PIN_DHT   5
#define PIN_RELAY 2
DHT dht(PIN_DHT, DHT11);

ElectinsIoT mqtt;

// ─── Handler relay ────────────────────────────────────────────────────────────
void onRelay(MqttParam& param) {
    bool on = param.asBool();
    digitalWrite(PIN_RELAY, on ? HIGH : LOW);
    mqtt.publish(TOPIC_RELAY_STATE, on ? "on" : "off", true /*retain*/);
    Serial.printf("[RELAY] %s\n", on ? "ON" : "OFF");
}

// ─── Connect callback ─────────────────────────────────────────────────────────
void onMqttConnected() {
    mqtt.subscribe(TOPIC_RELAY, onRelay, QOS1);
    Serial.println("[MQTT] Tersambung, relay siap dikontrol.");
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);
    dht.begin();

    // Mengaktifkan log internal bawaan library
    mqtt.setDebug(true);

    mqtt.onConnect(onMqttConnected);

    Serial.printf("\nMenghubungkan ke wifi (%s)...\n", WIFI_SSID);

    mqtt.begin(
        WIFI_SSID,   WIFI_PASS,
        MQTT_HOST,   MQTT_PORT,
        "DeviceID-Relay",
        MQTT_USER,   MQTT_PASS,
        USER_PREFIX, PROJECT_SLUG
    );
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    static uint32_t last = 0;
    if (millis() - last >= 5000) {
        last = millis();
        float temp = dht.readTemperature();
        float hum  = dht.readHumidity();
        if (!isnan(temp) && !isnan(hum)) {
            mqtt.publish(TOPIC_TEMP,     temp, 1);
            mqtt.publish(TOPIC_HUMIDITY, hum,  1);
            Serial.printf("[Sensor] Temp: %.1f°C  Hum: %.1f%%\n", temp, hum);
        }
    }
}
