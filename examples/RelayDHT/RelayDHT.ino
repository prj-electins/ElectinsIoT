/**
 * RelayDHT.ino — ElectinsIoT v2 Relay + DHT Sensor
 * ──────────────────────────────────────────────────
 * Kontrol relay via MQTT dan kirim data sensor DHT11/DHT22.
 *
 * Dependensi: DHT sensor library by Adafruit
 * TCP/MQTT: built-in SDK ESP32/ESP8266
 */

#include <ElectinsIoT.h>
#include <DHT.h>

// ─── Konfigurasi ──────────────────────────────────────────────────────────────
const char*    WIFI_SSID    = "YourSSID";
const char*    WIFI_PASS    = "YourPassword";
const char*    MQTT_HOST    = "iot.electins.id";
const char*    MQTT_USER    = "ID-XXXXXXXX";
const char*    MQTT_PASS    = "password";
const char*    PROJECT_SLUG = "prj";
const uint16_t MQTT_PORT    = 1883;

// ─── Topik ────────────────────────────────────────────────────────────────────
const char* TOPIC_TEMP        = "ID-XXXXXXXX/prj/temp";
const char* TOPIC_HUMIDITY    = "ID-XXXXXXXX/prj/humd";
const char* TOPIC_RELAY       = "ID-XXXXXXXX/prj/relay";
const char* TOPIC_RELAY_STATE = "ID-XXXXXXXX/prj/relay-state";

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

    mqtt.onConnect(onMqttConnected);

    mqtt.begin(
        WIFI_SSID,   WIFI_PASS,
        MQTT_HOST,   MQTT_PORT,
        "ID-XXXXXXXX",
        MQTT_USER,   MQTT_PASS,
        PROJECT_SLUG
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
