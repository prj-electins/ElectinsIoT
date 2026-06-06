# ElectinsIoT

> [English](#english) | [Indonesia](#indonesia)

---

<a name="english"></a>
## English

**v2.1.4** — Zero-dependency async MQTT library for ESP32 and ESP8266.  
One call in `setup()`. Nothing in `loop()`. No external libraries required.

### How It Works

```
WiFi management         → connects WiFi, auto-reconnects on drop
MQTT engine (1 owner)   → a dedicated FreeRTOS task (ESP32) or scheduled
                          function (ESP8266) is the ONLY owner of the socket:
                          connect, read, write, keepalive, reconnect
Outbox (thread-safe)    → publish()/subscribe() from any context just enqueue
                          packets — they never touch the socket directly
MQTT onConnect          → re-subscribe topics + publish "online" (retain=true)
Heartbeat (30s)         → publish "online" (retain=true) from engine context
LWT (broker-side)       → broker publishes "offline" on unexpected disconnect
```

The socket is never accessed from timer/ISR or WiFi-event contexts, so there
are no race conditions behind random reconnect/restart loops. `void loop()`
stays clean.

### No External Dependencies

The library uses only what is already built into the ESP32/ESP8266 SDK:

| Component | Source |
|---|---|
| MQTT 3.1.1 engine | Built-in (`ElectinsMqtt` — written from scratch) |
| TCP connection | `WiFiClient` (plain) or `WiFiClientSecure` (TLS) |
| Background engine | FreeRTOS task (ESP32) / scheduled function (ESP8266) |
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
    mqtt.subscribe("ID-XXXXXXXX/myproject/cmd", onCmd);
}

void setup() {
    Serial.begin(115200);
    mqtt.onConnect(onConnected);
    mqtt.begin("SSID", "pass", "iot.electins.id", 1883,
               "DeviceID",
               "PRJ-XXXXXXXX", "mqttpass",   // broker credentials
               "ID-XXXXXXXX",                // user prefix (REQUIRED)
               "myproject");                 // project slug
}

void loop() {
    // Nothing here — library handles everything
}
```

The library automatically:
- Connects to WiFi and MQTT
- Sets LWT → `ID-XXXXXXXX/myproject/$status = "offline"` (retain=true)
- Publishes `"online"` to `ID-XXXXXXXX/myproject/$status` on connect (retain=true)
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
    mqttUser,    // MQTT username (broker auth, e.g. "PRJ-XXXXXXXX")
    mqttPass,    // MQTT password
    userPrefix,  // user-owned topic prefix (e.g. "ID-XXXXXXXX") — REQUIRED
                 // → $status = <userPrefix>/<projectSlug>/$status
    projectSlug  // project slug (default: "device")
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

> **Hardware-sensitive callbacks (IR, NeoPixel, Servo, bit-banged peripherals).**  
> The library sends QoS 1 PUBACK before invoking your callback, so the network
> stack is idle while your callback runs. For long microsecond-precise routines
> like `irsend.sendGree()` or `FastLED.show()`, the recommended pattern is still
> to keep the callback short — set a flag and execute the long operation from
> `loop()`:
> ```cpp
> volatile bool g_doSend = false;
> int g_temp = 0;
>
> void onCmd(MqttParam& p) { g_temp = p.asInt(); g_doSend = true; }
>
> void loop() {
>     if (g_doSend) { g_doSend = false; irsend.sendGree(g_temp); }
> }
> ```

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

**v2.1.4** — Library async MQTT tanpa dependensi eksternal untuk ESP32 dan ESP8266.  
Satu panggilan di `setup()`. Tidak ada apapun di `loop()`. Tidak perlu install library lain.

### Cara Kerja

```
Manajemen WiFi          → menghubungkan WiFi, auto-reconnect saat terputus
Engine MQTT (1 pemilik) → satu FreeRTOS task khusus (ESP32) atau scheduled
                          function (ESP8266) jadi SATU-SATUNYA pemilik socket:
                          connect, baca, tulis, keepalive, reconnect
Outbox (thread-safe)    → publish()/subscribe() dari konteks mana pun hanya
                          menaruh paket ke antrian — tidak menyentuh socket
MQTT onConnect          → re-subscribe topik + publish "online" (retain=true)
Heartbeat (30 dt)       → publish "online" (retain=true) dari konteks engine
LWT (sisi broker)       → broker publish "offline" saat terputus tiba-tiba
```

Socket tidak pernah diakses dari konteks timer/ISR maupun WiFi-event, sehingga
tidak ada race condition penyebab loop reconnect/restart acak. `void loop()`
tetap bersih.

### Tanpa Dependensi Eksternal

Library hanya menggunakan komponen yang sudah ada di SDK ESP32/ESP8266:

| Komponen | Sumber |
|---|---|
| MQTT 3.1.1 engine | Built-in (`ElectinsMqtt` — ditulis dari nol) |
| Koneksi TCP | `WiFiClient` (plain) atau `WiFiClientSecure` (TLS) |
| Engine background | FreeRTOS task (ESP32) / scheduled function (ESP8266) |
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
    mqtt.subscribe("ID-XXXXXXXX/myproject/cmd", onCmd);
}

void setup() {
    Serial.begin(115200);
    mqtt.onConnect(onConnected);
    mqtt.begin("SSID", "pass", "iot.electins.id", 1883,
               "DeviceID",
               "PRJ-XXXXXXXX", "mqttpass",   // kredensial broker
               "ID-XXXXXXXX",                // user prefix (WAJIB)
               "myproject");                 // slug project
}

void loop() {
    // Kosong — library menangani segalanya
}
```

Library secara otomatis:
- Menghubungkan WiFi dan MQTT
- Mendaftarkan LWT → `ID-XXXXXXXX/myproject/$status = "offline"` (retain=true)
- Mempublish `"online"` ke `ID-XXXXXXXX/myproject/$status` saat connect (retain=true)
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
    mqttUser,    // Username MQTT (kredensial broker, mis. "PRJ-XXXXXXXX")
    mqttPass,    // Password MQTT
    userPrefix,  // Prefix topik milik pengguna (mis. "ID-XXXXXXXX") — WAJIB
                 // → $status = <userPrefix>/<projectSlug>/$status
    projectSlug  // Slug project (default: "device")
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

> **Callback yang sensitif timing (IR, NeoPixel, Servo, peripheral bit-bang).**  
> Library mengirim PUBACK QoS 1 SEBELUM memanggil callback Anda, jadi network
> stack idle selama callback berjalan. Untuk rutin presisi mikrodetik yang panjang
> seperti `irsend.sendGree()` atau `FastLED.show()`, pola yang dianjurkan tetap
> menjaga callback singkat — set flag, lalu eksekusi operasi panjang dari `loop()`:
> ```cpp
> volatile bool g_doSend = false;
> int g_temp = 0;
>
> void onCmd(MqttParam& p) { g_temp = p.asInt(); g_doSend = true; }
>
> void loop() {
>     if (g_doSend) { g_doSend = false; irsend.sendGree(g_temp); }
> }
> ```

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

Made with ❤️ by [Nash](mailto:support@electins.id) — [electins.id](https://electins.id)
