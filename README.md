# ElectinsIoT

> [English](#english) | [Indonesia](#indonesia)

---

<a name="english"></a>
## English

Arduino MQTT 3.1.1 library for ESP8266 and ESP32. Lightweight, no external dependencies, ready to use.

### Features

| Feature | Description |
|---|---|
| MQTT 3.1.1 | Connect, publish, subscribe, unsubscribe |
| QoS 0 / 1 / 2 | Full handshake for QoS 2 |
| Auto-reconnect | Configurable interval |
| Auto-resubscribe | Automatically resubscribes after reconnect |
| Last Will & Testament | LWT message on unexpected disconnect |
| TLS / SSL | `WiFiClientSecure` support (port 8883) |
| Per-topic callbacks | Separate handler per topic via `MqttParam` |
| Wildcard topics | `+` (single-level) and `#` (multi-level) |
| One-line setup | Single `begin()` call for WiFi + MQTT |
| Debug mode | Internal logs to Serial |

### Installation

Copy the `ElectinsIoT` folder into your Arduino `libraries` directory, then restart the IDE.

### Quick Start

```cpp
#include <ElectinsIoT.h>

WiFiClient wifiClient;
ElectinsIoT mqtt(wifiClient);

void onConnected() {
    mqtt.publish("device/status", "online", true);
    mqtt.subscribe("device/cmd", [](MqttParam& p) {
        Serial.println(p.asStr());
    });
}

void setup() {
    Serial.begin(115200);
    mqtt.setWill("device/status", "offline", true);
    mqtt.onConnect(onConnected);
    mqtt.begin("SSID", "PASSWORD", "broker.example.com", 1883, "DeviceID");
}

void loop() {
    mqtt.run();
}
```

### Publish

```cpp
mqtt.publish("sensor/temp",    25.4f);               // float
mqtt.publish("sensor/counter", 100);                 // int
mqtt.publish("relay/state",    true);                // bool → "true"/"false"
mqtt.publish("info/msg",       "hello world");       // string
mqtt.publish("info/msg",       "hello", true, QOS1); // retain + QoS
mqtt << "info/short:payload";                        // shorthand operator
```

### Subscribe

```cpp
// Per-topic callback
mqtt.subscribe("relay/ctrl", [](MqttParam& p) {
    digitalWrite(PIN_RELAY, p.asBool() ? HIGH : LOW);
}, QOS1);

// Multi-level wildcard
mqtt.subscribe("device/#", onAny);

// Single-level wildcard
mqtt.subscribe("sensor/+/temp", onTemp);

mqtt.unsubscribe("relay/ctrl");
```

### Configuration

```cpp
mqtt.setServer("broker.example.com", 1883);
mqtt.setCredentials("user", "pass");
mqtt.setClientId("DeviceID");
mqtt.setWill("device/status", "offline", true, QOS1);
mqtt.setKeepAlive(30);            // seconds, default 60
mqtt.setBufferSize(1024);         // bytes, default 512
mqtt.setDebug(true);              // enable Serial logs
mqtt.enableReconnect(true, 3000); // enable, interval 3s
```

### Callbacks

```cpp
mqtt.onConnect([]() { /* called on every connect / reconnect */ });
mqtt.onDisconnect([]() { /* called on disconnect */ });
mqtt.onMessage([](const char* topic, const uint8_t* payload, uint16_t len) {
    // global fallback for all incoming messages
});
```

### MqttParam

```cpp
void handler(MqttParam& p) {
    p.asStr();    // const char*
    p.asInt();    // int
    p.asFloat();  // float
    p.asBool();   // true if "1", "true", or "on"
    p.length();   // payload length (uint16_t)
    p.data();     // raw bytes (const uint8_t*)
}
```

### JSON Helper (ArduinoJson)

Install [ArduinoJson](https://arduinojson.org) via Library Manager, then include it **before** ElectinsIoT:

```cpp
#include <ArduinoJson.h>   // must be first
#include <ElectinsIoT.h>
```

**Publish JSON:**
```cpp
StaticJsonDocument<128> doc;
doc["temp"]   = 25.4;
doc["uptime"] = millis() / 1000;
doc["status"] = "ok";

mqtt.publishJson("device/sensor", doc);
mqtt.publishJson("device/sensor", doc, true, QOS1); // retain + QoS
```

**Subscribe JSON:**
```cpp
mqtt.subscribeJson("device/config", [](const char* topic, JsonDocument& doc) {
    int interval = doc["interval"] | 5000;
    bool debug   = doc["debug"]    | false;
    Serial.printf("interval=%d debug=%d\n", interval, debug);
});
```

---

### TLS / Secure MQTT (port 8883)

```cpp
#include <WiFiClientSecure.h>

WiFiClientSecure secureClient;
ElectinsIoT mqtt(secureClient);

void setup() {
    secureClient.setInsecure(); // skip certificate verification
    mqtt.begin(ssid, pass, host, 8883, clientId);
}
```

### Examples

| Example | Description |
|---|---|
| `BasicIoT` | Basic connect, publish, subscribe |
| `AdvancedIoT` | QoS 0/1/2, wildcard, per-topic callbacks, debug |
| `SecureIoT` | TLS connection on port 8883 |
| `JsonIoT` | JSON publish & subscribe with ArduinoJson |
| `RelayDHT` | Relay control + DHT11 temperature & humidity monitoring |

---

<a name="indonesia"></a>
## Indonesia

Library Arduino MQTT 3.1.1 untuk ESP8266 dan ESP32. Ringan, tanpa dependensi eksternal, siap pakai.

### Fitur

| Fitur | Keterangan |
|---|---|
| MQTT 3.1.1 | Connect, publish, subscribe, unsubscribe |
| QoS 0 / 1 / 2 | Handshake penuh untuk QoS 2 |
| Auto-reconnect | Interval dapat dikonfigurasi |
| Auto-resubscribe | Otomatis subscribe ulang setelah reconnect |
| Last Will & Testament | Pesan LWT saat koneksi terputus tak terduga |
| TLS / SSL | Dukungan `WiFiClientSecure` (port 8883) |
| Per-topic callback | Handler terpisah per topik via `MqttParam` |
| Wildcard | `+` (single-level) dan `#` (multi-level) |
| One-line setup | Satu baris `begin()` untuk WiFi + MQTT |
| Debug mode | Log internal ke Serial |

### Instalasi

Salin folder `ElectinsIoT` ke direktori `libraries` Arduino, lalu restart IDE.

### Contoh Cepat

```cpp
#include <ElectinsIoT.h>

WiFiClient wifiClient;
ElectinsIoT mqtt(wifiClient);

void onConnected() {
    mqtt.publish("perangkat/status", "online", true);
    mqtt.subscribe("perangkat/perintah", [](MqttParam& p) {
        Serial.println(p.asStr());
    });
}

void setup() {
    Serial.begin(115200);
    mqtt.setWill("perangkat/status", "offline", true);
    mqtt.onConnect(onConnected);
    mqtt.begin("SSID", "PASSWORD", "broker.example.com", 1883, "DeviceID");
}

void loop() {
    mqtt.run();
}
```

### Publish

```cpp
mqtt.publish("sensor/suhu",    25.4f);                  // float
mqtt.publish("sensor/counter", 100);                    // int
mqtt.publish("relay/state",    true);                   // bool → "true"/"false"
mqtt.publish("info/pesan",     "halo dunia");           // string
mqtt.publish("info/pesan",     "halo", true, QOS1);     // retain + QoS
mqtt << "info/singkat:isi pesan";                       // shorthand operator
```

### Subscribe

```cpp
// Callback per topik
mqtt.subscribe("relay/kontrol", [](MqttParam& p) {
    digitalWrite(PIN_RELAY, p.asBool() ? HIGH : LOW);
}, QOS1);

// Wildcard multi-level
mqtt.subscribe("perangkat/#", onSemua);

// Wildcard single-level
mqtt.subscribe("sensor/+/suhu", onSuhu);

mqtt.unsubscribe("relay/kontrol");
```

### Konfigurasi

```cpp
mqtt.setServer("broker.example.com", 1883);
mqtt.setCredentials("user", "pass");
mqtt.setClientId("DeviceID");
mqtt.setWill("perangkat/status", "offline", true, QOS1);
mqtt.setKeepAlive(30);            // detik, default 60
mqtt.setBufferSize(1024);         // byte, default 512
mqtt.setDebug(true);              // aktifkan log ke Serial
mqtt.enableReconnect(true, 3000); // aktifkan, interval 3 detik
```

### Callbacks

```cpp
mqtt.onConnect([]() { /* dipanggil setiap connect/reconnect */ });
mqtt.onDisconnect([]() { /* dipanggil saat terputus */ });
mqtt.onMessage([](const char* topic, const uint8_t* payload, uint16_t len) {
    // fallback global untuk semua pesan masuk
});
```

### MqttParam

```cpp
void handler(MqttParam& p) {
    p.asStr();    // const char*
    p.asInt();    // int
    p.asFloat();  // float
    p.asBool();   // true jika "1", "true", atau "on"
    p.length();   // panjang payload (uint16_t)
    p.data();     // raw bytes (const uint8_t*)
}
```

### JSON Helper (ArduinoJson)

Install [ArduinoJson](https://arduinojson.org) via Library Manager, lalu include **sebelum** ElectinsIoT:

```cpp
#include <ArduinoJson.h>   // harus di atas
#include <ElectinsIoT.h>
```

**Publish JSON:**
```cpp
StaticJsonDocument<128> doc;
doc["suhu"]   = 25.4;
doc["uptime"] = millis() / 1000;
doc["status"] = "ok";

mqtt.publishJson("perangkat/sensor", doc);
mqtt.publishJson("perangkat/sensor", doc, true, QOS1); // retain + QoS
```

**Subscribe JSON:**
```cpp
mqtt.subscribeJson("perangkat/config", [](const char* topic, JsonDocument& doc) {
    int interval = doc["interval"] | 5000;
    bool debug   = doc["debug"]    | false;
    Serial.printf("interval=%d debug=%d\n", interval, debug);
});
```

---

### TLS / Koneksi Aman (port 8883)

```cpp
#include <WiFiClientSecure.h>

WiFiClientSecure secureClient;
ElectinsIoT mqtt(secureClient);

void setup() {
    secureClient.setInsecure(); // lewati verifikasi sertifikat
    mqtt.begin(ssid, pass, host, 8883, clientId);
}
```

### Daftar Contoh

| Contoh | Deskripsi |
|---|---|
| `BasicIoT` | Koneksi dasar, publish, subscribe |
| `AdvancedIoT` | QoS 0/1/2, wildcard, per-topic callback, debug |
| `SecureIoT` | Koneksi TLS pada port 8883 |
| `JsonIoT` | Publish & subscribe JSON dengan ArduinoJson |
| `RelayDHT` | Kontrol relay + monitoring suhu & kelembaban DHT11 |

---

Made with ❤️ by [Nash](mailto:nash@electins.id) — [electins.id](https://electins.id)
