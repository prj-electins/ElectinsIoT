# ElctinsIoTClient

Lightweight MQTT 3.1.1 client for ESP8266 and ESP32. Simple, powerful, and more ergonomic than PubSubClient.

## Features

- MQTT 3.1.1 — connect, publish, subscribe, unsubscribe
- QoS 0, 1, and 2 (full handshake)
- Auto-reconnect with configurable interval
- Auto-resubscribe after reconnect
- Last Will & Testament (LWT)
- TLS/SSL support via `WiFiClientSecure` (port 8883)
- Per-topic callbacks with `MqttParam` helper
- Wildcard topics (`+` single-level, `#` multi-level)
- `onConnect` / `onDisconnect` callbacks
- Global fallback `onMessage` callback
- One-line `begin()` setup
- Debug mode via `setDebug(true)`
- No external dependencies

---

## Installation

Copy the `ElctinsIoTClient` folder into your Arduino `libraries` directory, then restart the IDE.

---

## Quick Start

```cpp
#include <ElctinsIoTClient.h>
#include <ESP8266WiFi.h> // or <WiFi.h> for ESP32

WiFiClient wifiClient;
ElctinsIoTClient mqtt(wifiClient);

void onCmd(MqttParam& param) {
    Serial.println(param.asStr());
}

void onConnected() {
    mqtt.publish("device/status", "online", true);
    mqtt.subscribe("device/cmd", onCmd);
}

void setup() {
    Serial.begin(115200);
    mqtt.setWill("device/status", "offline", true);
    mqtt.onConnect(onConnected);
    mqtt.begin("SSID", "PASSWORD", "broker.example.com", 1883, "DeviceID", "user", "pass");
}

void loop() {
    mqtt.run();
}
```

---

## API Reference

### Setup (call before `begin()`)

| Method | Description |
|---|---|
| `setWill(topic, payload, retain, qos)` | Set Last Will message |
| `setKeepAlive(seconds)` | Set keepalive interval (default: 60s) |
| `setBufferSize(bytes)` | Set packet buffer size (default: 512) |
| `setDebug(true)` | Enable internal debug logs to Serial |
| `enableReconnect(true, intervalMs)` | Enable auto-reconnect (default: enabled, 5000ms) |
| `onConnect(callback)` | Called on every connect/reconnect |
| `onDisconnect(callback)` | Called on disconnect |
| `onMessage(callback)` | Global fallback for all incoming messages |

### Connect

```cpp
mqtt.begin(ssid, wifiPass, host, port, clientId, user, mqttPass); // one-line
mqtt.connect();
mqtt.connect("ClientID");
mqtt.connect("ClientID", "user", "pass");
mqtt.disconnect();
bool ok = mqtt.connected();
```

### Publish

```cpp
mqtt.publish("topic", "string");
mqtt.publish("topic", 42);               // int
mqtt.publish("topic", 25.6f);            // float (2 decimals)
mqtt.publish("topic", 25.6f, 3);         // float (3 decimals)
mqtt.publish("topic", true);             // bool → "true"/"false"
mqtt.publish("topic", "msg", true);      // with retain
mqtt.publish("topic", "msg", false, QOS1); // with QoS
mqtt << "topic:payload";                 // shorthand
```

### Subscribe

```cpp
// No callback — use global onMessage
mqtt.subscribe("topic");
mqtt.subscribe("topic", QOS1);

// Per-topic callback with MqttParam
mqtt.subscribe("topic", handlerFn);
mqtt.subscribe("topic", handlerFn, QOS1);

// Wildcard
mqtt.subscribe("device/#", handlerFn);   // all sub-topics
mqtt.subscribe("sensor/+/temp", handlerFn); // single-level wildcard

mqtt.unsubscribe("topic");
```

### MqttParam

```cpp
void onData(MqttParam& param) {
    param.asStr();    // const char* (null-terminated)
    param.asInt();    // int
    param.asFloat();  // float
    param.asBool();   // true if "1", "true", or "on"
    param.length();   // uint16_t payload length
    param.data();     // const uint8_t* raw bytes
}
```

---

## TLS / Secure MQTT (port 8883)

```cpp
#include <WiFiClientSecure.h>
WiFiClientSecure secureClient;
ElctinsIoTClient mqtt(secureClient);

secureClient.setInsecure(); // skip certificate verification
mqtt.begin(..., 8883, ...);
```

---

## Examples

| Example | Description |
|---|---|
| `BasicMQTT` | Simple connect, publish, subscribe |
| `AdvancedMQTT` | QoS 0/1/2, per-topic callbacks, wildcard, debug |
| `SecureMQTT` | TLS connection on port 8883 |
| `RelayDHT` | ESP32 relay control + DHT11 temperature & humidity monitoring |
