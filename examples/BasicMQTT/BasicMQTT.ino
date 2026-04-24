#include <ElctinsIoTClient.h>
#include <WiFi.h>

const char* WIFI_SSID  = "YourSSID";
const char* WIFI_PASS  = "YourPassword";
const char* MQTT_HOST  = "broker.example.com";
const char* MQTT_USER  = "username";
const char* MQTT_PASS  = "password";
const uint16_t MQTT_PORT = 1883;

const char* TOPIC_STATUS = "device/status";
const char* TOPIC_CMD    = "device/cmd";
const char* TOPIC_TEMP   = "device/temp";

WiFiClient wifiClient;
ElctinsIoTClient mqtt(wifiClient);

void onCmd(MqttParam& param) {
    Serial.println(param.asStr());
}

void onConnected() {
    mqtt.publish(TOPIC_STATUS, "online", true);
    mqtt.subscribe(TOPIC_CMD, onCmd);
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
    mqtt.setWill(TOPIC_STATUS, "offline", true);
    mqtt.onConnect(onConnected);
    mqtt.begin(WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, "DeviceID-01", MQTT_USER, MQTT_PASS);
}

void loop() {
    reconnectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;

    mqtt.run();

    static uint32_t last = 0;
    if (millis() - last >= 10000) {
        last = millis();
        mqtt.publish(TOPIC_TEMP, 25.0f + random(0, 50) / 10.0f);
    }
}
