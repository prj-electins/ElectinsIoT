# ElectinsIoT

> [English](#english) | [Indonesia](#indonesia)

---

<a name="english"></a>
## English

**v2.0.0** — Zero-dependency async MQTT library for ESP32 and ESP8266.  
One call in `setup()`. Nothing in `loop()`. No external libraries required.

### How It Works

```
WiFi event (GOT_IP)    → triggers MQTT connect
MQTT onConnect         → publish "online" + start heartbeat Ticker
MQTT onDisconnect      → stop heartbeat + schedule reconnect via Ticker
Poll Ticker (10ms)     → reads TCP buffer non-blocking — processes packets
Heartbeat Ticker (30s) → publish "online" (retain=true)
LWT (broker-side)      → publish "offline" on unexpected disconnect
```

Everything runs in the background. `void loop()` stays clean.

### No External Dependencies

The library uses only what is already built into the ESP32/ESP8266 SDK:

| Component | Source |
|---|---|
| MQTT 3.1.1 engine | Built-in (`ElectinsMqtt` — written from scratch) |
| TCP connection | `WiFiClient` (plain) or `WiFiClientSecure` (TLS) |
| Background polling | `Ticker` |
| WiFi management | `WiFi` / `ESP8266WiFi` |

The only optional dependency is **ArduinoJson** — only needed if you use `publishJson()` / `subscribeJson()`.

### Installation

1. Copy the `ElectinsIoT` folder into your Arduino `libraries` directory.
2. Restart the IDE.
3. Done — no other libraries to install.

### Quick Start

```cpp
#include <ElectinsIoT.h>

ElectinsIoT mqtt;

void onCmd(MqttParam& p) {
    Serial.println(p.asStr());
}

void onConnected() {
    mqtt.subscribe("user/proj/cmd", onCmd);
}

void setup() {
    Serial.begin(115200);
    mqtt.onConnect(onConnected);
    mqtt.begin("SSID", "pass", "iot.electins.id", 1883,
               "DeviceID", "user", "mqttpass", "proj");
}

void loop() {
    // Nothing here — library handles everything
}
```

The library automatically:
- Connects to WiFi and MQTT
- Sets LWT → `user/proj/$status = "offline"` (retain=true)
- Publishes `"online"` to `user/proj/$status` on connect (retain=true)
- Sends heartbeat `"online"` every 30 seconds
- Reconnects WiFi and MQTT on any disconnect
- Re-subscribes all topics after reconnect

### `begin()` Parameters

```cpp
mqtt.begin(
    ssid,        // WiFi SSID
    wifiPass,    // WiFi password
    mqttHost,    // broker hostname or IP
    mqttPort,    // broker port (1883 plain, 8883 TLS)
    clientId,    // unique device client ID
    mqttUser,    // MQTT username  →  also builds $status topic
    mqttPass,    // MQTT password
    projectSlug  // project slug   →  $status = <user>/<slug>/$status
                 // default: "device"
);
```

### Publish

```cpp
mqtt.publish("topic", "string");               // string
mqtt.publish("topic", 25.4f);                  // float
mqtt.publish("topic", 100);                    // int
mqtt.publish("topic", true);                   // bool → "true"/"false"
mqtt.publish("topic", "msg", true, QOS1);      // retain + QoS1
mqtt << "topic:payload";                       // shorthand operator
```

### Subscribe

```cpp
// MqttParam callback — .asStr(), .asInt(), .asFloat(), .asBool()
mqtt.subscribe("user/proj/cmd", onCmd, QOS1);

// Raw string callback
mqtt.subscribe("user/proj/data", [](const char* p, size_t len) {
    Serial.printf("%.*s\n", (int)len, p);
});

// Wildcard
mqtt.subscribe("user/proj/#", onAny);   // multi-level
mqtt.subscribe("sensor/+/temp", onSensor); // single-level

mqtt.unsubscribe("user/proj/cmd");
```

### Callbacks

```cpp
mqtt.onConnect([]() { /* called on every connect / reconnect */ });
mqtt.onDisconnect([]() { /* called on disconnect */ });
mqtt.onMessage([](const char* topic, const char* payload, size_t len) {
    // global fallback — called for all incoming messages
});
```

### Configuration (call before begin)

```cpp
mqtt.setDebug(true);               // Serial debug logs
mqtt.setKeepAlive(30);             // MQTT keepalive, seconds (default 15)
mqtt.setReconnectInterval(10);     // reconnect retry interval, seconds (default 5)
mqtt.setHeartbeatInterval(60);     // heartbeat interval, seconds (default 30)
```

### Status

```cpp
mqtt.connected()     // bool — true if MQTT is connected
mqtt.statusTopic()   // const char* — e.g. "username/myproject/$status"
```

### MqttParam Helper

```cpp
void handler(MqttParam& p) {
    p.asStr();    // const char*
    p.asInt();    // int
    p.asFloat();  // float
    p.asBool();   // true if "1", "true", or "on"
    p.length();   // payload size (size_t)
}
```

### TLS / Secure MQTT (port 8883)

```cpp
// Call before begin()
mqtt.setSecure(true);

// Option A: skip certificate verification (development / self-signed broker)
mqtt.setInsecure(true);

// Option B: use CA certificate (production)
mqtt.setInsecure(false);
// Note: to provide a CA cert, use WiFiClientSecure directly before calling begin()

mqtt.begin(..., 8883, ...);
```

### JSON Helper (optional — requires ArduinoJson)

Install [ArduinoJson](https://arduinojson.org) via Library Manager, then include it **before** ElectinsIoT:

```cpp
#include <ArduinoJson.h>   // must come first
#include <ElectinsIoT.h>
```

```cpp
// Publish JSON
JsonDocument doc;
doc["temp"] = 25.4;
doc["uptime"] = millis() / 1000;
mqtt.publishJson("user/proj/sensor", doc);
mqtt.publishJson("user/proj/sensor", doc, true, QOS1); // retain + QoS1

// Subscribe JSON — payload auto-parsed into JsonDocument
mqtt.subscribeJson("user/proj/config", [](const char* topic, JsonDocument& doc) {
    int interval = doc["interval"] | 5000;
});
```

### Examples

| Example | Description |
|---|---|
| `BasicIoT` | Minimal setup — connect, publish, subscribe |
| `AdvancedIoT` | QoS 0/1, wildcard, per-topic callbacks, disconnect handler |
| `SecureIoT` | TLS connection on port 8883 |
| `JsonIoT` | JSON publish & subscribe with ArduinoJson |
| `RelayDHT` | Relay control + DHT11 temperature & humidity |

---

<a name="indonesia"></a>
## Indonesia

**v2.0.0** — Library async MQTT tanpa dependensi eksternal untuk ESP32 dan ESP8266.  
Satu panggilan di `setup()`. Tidak ada apapun di `loop()`. Tidak perlu install library lain.

### Cara Kerja

```
Event WiFi (GOT_IP)      → memicu koneksi MQTT
MQTT onConnect           → publish "online" + mulai Ticker heartbeat
MQTT onDisconnect        → hentikan heartbeat + jadwalkan reconnect via Ticker
Poll Ticker (10ms)       → baca TCP buffer non-blocking — proses paket
Heartbeat Ticker (30 dt) → publish "online" (retain=true)
LWT (sisi broker)        → publish "offline" saat koneksi terputus tiba-tiba
```

Semua berjalan di background. `void loop()` tetap bersih.

### Tanpa Dependensi Eksternal

Library hanya menggunakan komponen yang sudah ada di SDK ESP32/ESP8266:

| Komponen | Sumber |
|---|---|
| MQTT 3.1.1 engine | Built-in (`ElectinsMqtt` — ditulis dari nol) |
| Koneksi TCP | `WiFiClient` (plain) atau `WiFiClientSecure` (TLS) |
| Background polling | `Ticker` |
| Manajemen WiFi | `WiFi` / `ESP8266WiFi` |

Satu-satunya dependensi opsional adalah **ArduinoJson** — hanya diperlukan jika Anda menggunakan `publishJson()` / `subscribeJson()`.

### Instalasi

1. Salin folder `ElectinsIoT` ke direktori `libraries` Arduino.
2. Restart IDE.
3. Selesai — tidak perlu install library lain.

### Contoh Cepat

```cpp
#include <ElectinsIoT.h>

ElectinsIoT mqtt;

void onCmd(MqttParam& p) {
    Serial.println(p.asStr());
}

void onConnected() {
    mqtt.subscribe("user/proj/cmd", onCmd);
}

void setup() {
    Serial.begin(115200);
    mqtt.onConnect(onConnected);
    mqtt.begin("SSID", "pass", "iot.electins.id", 1883,
               "DeviceID", "user", "mqttpass", "proj");
}

void loop() {
    // Kosong — library menangani segalanya
}
```

Library secara otomatis:
- Menghubungkan WiFi dan MQTT
- Mendaftarkan LWT → `user/proj/$status = "offline"` (retain=true)
- Mempublish `"online"` ke `user/proj/$status` saat connect (retain=true)
- Mengirim heartbeat `"online"` setiap 30 detik
- Reconnect WiFi dan MQTT saat koneksi terputus
- Re-subscribe semua topik setelah reconnect

### Parameter `begin()`

```cpp
mqtt.begin(
    ssid,        // SSID WiFi
    wifiPass,    // Password WiFi
    mqttHost,    // Hostname/IP broker
    mqttPort,    // Port broker (1883 plain, 8883 TLS)
    clientId,    // Client ID unik perangkat
    mqttUser,    // Username MQTT → digunakan untuk membangun topik $status
    mqttPass,    // Password MQTT
    projectSlug  // Slug project → $status = <user>/<slug>/$status
                 // default: "device"
);
```

### Publish

```cpp
mqtt.publish("topik", "string");               // string
mqtt.publish("topik", 25.4f);                  // float
mqtt.publish("topik", 100);                    // int
mqtt.publish("topik", true);                   // bool → "true"/"false"
mqtt.publish("topik", "pesan", true, QOS1);    // retain + QoS1
mqtt << "topik:isi";                           // shorthand operator
```

### Subscribe

```cpp
// Callback MqttParam — .asStr(), .asInt(), .asFloat(), .asBool()
mqtt.subscribe("user/proj/cmd", onCmd, QOS1);

// Callback raw string
mqtt.subscribe("user/proj/data", [](const char* p, size_t len) {
    Serial.printf("%.*s\n", (int)len, p);
});

// Wildcard
mqtt.subscribe("user/proj/#", onSemua);     // multi-level
mqtt.subscribe("sensor/+/suhu", onSuhu);   // single-level

mqtt.unsubscribe("user/proj/cmd");
```

### Callbacks

```cpp
mqtt.onConnect([]() { /* dipanggil setiap connect/reconnect */ });
mqtt.onDisconnect([]() { /* dipanggil saat terputus */ });
mqtt.onMessage([](const char* topic, const char* payload, size_t len) {
    // fallback global — dipanggil untuk semua pesan masuk
});
```

### Konfigurasi (panggil sebelum begin)

```cpp
mqtt.setDebug(true);               // log debug ke Serial
mqtt.setKeepAlive(30);             // keepalive MQTT, detik (default 15)
mqtt.setReconnectInterval(10);     // interval reconnect, detik (default 5)
mqtt.setHeartbeatInterval(60);     // interval heartbeat, detik (default 30)
```

### Status

```cpp
mqtt.connected()     // bool — true jika MQTT tersambung
mqtt.statusTopic()   // const char* — misal "username/myproject/$status"
```

### MqttParam Helper

```cpp
void handler(MqttParam& p) {
    p.asStr();    // const char*
    p.asInt();    // int
    p.asFloat();  // float
    p.asBool();   // true jika "1", "true", atau "on"
    p.length();   // ukuran payload (size_t)
}
```

### TLS / Koneksi Aman (port 8883)

```cpp
// Panggil sebelum begin()
mqtt.setSecure(true);

// Opsi A: skip verifikasi sertifikat (development / broker self-signed)
mqtt.setInsecure(true);

// Opsi B: gunakan CA certificate (production)
mqtt.setInsecure(false);

mqtt.begin(..., 8883, ...);
```

### JSON Helper (opsional — memerlukan ArduinoJson)

Install [ArduinoJson](https://arduinojson.org) via Library Manager, lalu include **sebelum** ElectinsIoT:

```cpp
#include <ArduinoJson.h>   // harus di atas
#include <ElectinsIoT.h>
```

```cpp
// Publish JSON
JsonDocument doc;
doc["suhu"] = 25.4;
doc["uptime"] = millis() / 1000;
mqtt.publishJson("user/proj/sensor", doc);
mqtt.publishJson("user/proj/sensor", doc, true, QOS1); // retain + QoS1

// Subscribe JSON — payload di-parse otomatis ke JsonDocument
mqtt.subscribeJson("user/proj/config", [](const char* topic, JsonDocument& doc) {
    int interval = doc["interval"] | 5000;
});
```

### Daftar Contoh

| Contoh | Deskripsi |
|---|---|
| `BasicIoT` | Setup minimal — koneksi, publish, subscribe |
| `AdvancedIoT` | QoS 0/1, wildcard, per-topic callback, handler disconnect |
| `SecureIoT` | Koneksi TLS pada port 8883 |
| `JsonIoT` | Publish & subscribe JSON dengan ArduinoJson |
| `RelayDHT` | Kontrol relay + monitoring DHT11 |

---

Made with ❤️ by [Nash](mailto:info@electins.id) — [electins.id](https://electins.id)
