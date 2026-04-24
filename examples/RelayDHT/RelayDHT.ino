#include <ElctinsIoTClient.h>
#include <WiFi.h>
#include <DHT.h>

const char* WIFI_SSID  = "YourSSID";
const char* WIFI_PASS  = "YourPassword";
const char* MQTT_HOST  = "broker.example.com";
const char* MQTT_USER  = "username";
const char* MQTT_PASS  = "password";
const uint16_t MQTT_PORT = 1883;

#define PIN_DHT   5
#define PIN_RELAY 2
DHT dht(PIN_DHT, DHT11);

const char* TOPIC_STATUS      = "ID-XXXXXXXX/prj/status";
const char* TOPIC_TEMP        = "ID-XXXXXXXX/prj/temp";
const char* TOPIC_HUMIDITY    = "ID-XXXXXXXX/prj/humd";
const char* TOPIC_RELAY       = "ID-XXXXXXXX/prj/relay";
const char* TOPIC_RELAY_STATE = "ID-XXXXXXXX/prj/relay-state";

WiFiClient wifiClient;
ElctinsIoTClient mqtt(wifiClient);

void onRelay(MqttParam& param) {
    if (param.asBool()) {
        digitalWrite(PIN_RELAY, HIGH);
        mqtt.publish(TOPIC_RELAY_STATE, "on", true);
    } else {
        digitalWrite(PIN_RELAY, LOW);
        mqtt.publish(TOPIC_RELAY_STATE, "off", true);
    }
}

void onConnected() {
    mqtt.publish(TOPIC_STATUS, "online", true);
    mqtt.subscribe(TOPIC_RELAY, onRelay, QOS1);
}

void reconnectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);
    dht.begin();

    mqtt.setWill(TOPIC_STATUS, "offline", true);
    mqtt.onConnect(onConnected);
    mqtt.begin(WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, "ID-XXXXXXXX", MQTT_USER, MQTT_PASS);
}

void loop() {
    reconnectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;

    mqtt.run();

    static uint32_t last = 0;
    if (millis() - last >= 10000) {
        last = millis();
        float temp = dht.readTemperature();
        float hum  = dht.readHumidity();
        if (!isnan(temp) && !isnan(hum)) {
            mqtt.publish(TOPIC_TEMP,     temp, 1);
            mqtt.publish(TOPIC_HUMIDITY, hum,  1);
        }
    }
}
