# ElectinsIoT

> [English](#english) | [Indonesia](#indonesia)

---

<a name="english"></a>
## English

**v3.0.2** — Thread-safe, zero-dependency automatic background TCP & Protobuf library for ESP32 and ESP8266.  
One call in `setup()`. Nothing in `loop()`. No external libraries required.

### How It Works

```
WiFi management         → connects WiFi, auto-reconnects on drop
TCP/Protobuf engine     → a dedicated FreeRTOS task (ESP32) or Ticker
                          (ESP8266) is the ONLY owner of the socket:
                          connect, read, write, keepalive, reconnect
Local Cache (Thread-safe) → stores downlink param state, read instantly via
                          getBool(), getDouble(), getString()
Outbox (Thread-safe)    → sendTelemetry() / sendBatch() from any context
                          writes to buffer — never blocks main thread (non-blocking)
Heartbeat (8s)          → sends ping with fw_version automatically
```

The socket is never accessed directly from the main loop context, preventing socket race conditions and crash loops. `void loop()` stays clean and free of network boilerplate.

### No External Dependencies

The library uses only what is already built into the ESP32/ESP8266 SDK:

| Component | Source |
|---|---|
| TCP Protobuf engine | Built-in (`ElectinsIoT` — written from scratch with inline Protobuf) |
| TCP connection | `WiFiClient` (plain) or `WiFiClientSecure` (TLS) |
| Background engine | FreeRTOS task (ESP32) / async Ticker (ESP8266) |
| WiFi management | `WiFi` / `ESP8266WiFi` |

---

### Installation

1. Copy the `ElectinsIoT` folder into your Arduino `libraries` directory.
2. Restart the IDE.
3. Done — no other libraries to install.

---

### Quick Start

```cpp
#include <ElectinsIoT.h>

const char* WIFI_SSID    = "YOUR_WIFI_SSID";
const char* WIFI_PASS    = "YOUR_WIFI_PASSWORD";
const char* API_KEY      = "YOUR_API_KEY";
const char* FIRMWARE_VER = "1.0.0";

const char* PARAM_LED    = "led";
const char* PARAM_SUHU   = "suhu";

WiFiClient client;
ElectinsIoT iot(client);

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);

    iot.setDebug(true);

    // Automatically manages WiFi, TCP socket, keepalives, and OTA updates in the background
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER);
}

void loop() {
    // 1. Read parameter from local cache
    bool ledState = iot.getBool(PARAM_LED, false);
    if (ledState) {
        digitalWrite(LED_BUILTIN, HIGH);
    } else {
        digitalWrite(LED_BUILTIN, LOW);
    }

    // 2. Send telemetry every 3 seconds
    static unsigned long lastSend = 0;
    if (millis() - lastSend >= 3000) {
        lastSend = millis();
        if (iot.connected()) {
            float temperature = 25.0f + random(0, 50) / 10.0f;
            iot.sendTelemetry(PARAM_SUHU, temperature);
        }
    }
}
```

---

### API Reference

#### Initialization & Reconnections
- **`iot.beginWiFi(apiKey, ssid, pass, version = "1.0.0", useSsl = false, deviceId = nullptr)`**  
  Initializes and starts the automated background connection loop. Automatically handles Wi-Fi connection, TCP connection, ping heartbeats, and OTA updates. If `useSsl` is `true`, it connects to port `8883` (requires passing a `WiFiClientSecure` instance to the constructor). If `useSsl` is `false`, it connects to port `1883` by default.
- **`iot.connected()`**  
  Returns `true` if currently connected to the server.
- **`iot.disconnect()`**  
  Closes the TCP socket connection.

#### Local Cache Polling (GET)
- **`iot.getBool(param, defaultValue = false)`**  
  Reads a boolean parameter state.
- **`iot.getDouble(param, defaultValue = 0.0)`**  
  Reads a double parameter value.
- **`iot.getString(param, defaultValue = "")`**  
  Reads a string parameter (pointer is thread-safe and stable).

#### Telemetry Sending (SET)
- **`iot.sendTelemetry(param, value)`** - Sends a numeric parameter.
- **`iot.sendTelemetryString(param, value)`** - Sends a string parameter.
- **`iot.sendTelemetryBool(param, value)`** - Sends a boolean parameter.

#### Batch Telemetry (Multiple Parameters in One Packet)
- **`iot.startBatch()`** - Starts a new batch buffer.
- **`iot.addBatch(param, value)`** - Adds a numeric parameter.
- **`iot.addBatchString(param, value)`** - Adds a string parameter.
- **`iot.addBatchBool(param, value)`** - Adds a boolean parameter.
- **`iot.sendBatch()`** - Sends the accumulated batch payload.

#### Callbacks
- **`iot.onUpdateParam(cb)`**  
  `void cb(const char* param, double value, const char* stringValue)` - Triggered when a parameter is modified from the server.
- **`iot.onOtaUpdate(cb)`**  
  `void cb(const char* firmwareUrl)` - Triggered when an OTA firmware update is ordered.
- **`iot.onReboot(cb)`**  
  `void cb()` - Triggered before the device executes `ESP.restart()`.

#### Configuration
- **`iot.setDebug(enable)`** - Enable or disable serial debug prints.
- **`iot.setKeepAlive(seconds)`** - Adjust ping heartbeat interval (default is 8s).

---

### Secure Connection / TLS (Port 8883)

To connect securely, configure and pass a `WiFiClientSecure` instance to the constructor:

```cpp
#include <WiFiClientSecure.h>

WiFiClientSecure secureClient;
ElectinsIoT iot(secureClient);

void setup() {
    // Configure certificate validation on WiFiClientSecure
    secureClient.setCACert(ROOT_CA_CERTIFICATE); // Or setInsecure() for testing
    
    // Automatically manages WiFi and TLS TCP connection in the background
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER, true);
}
```

---

<a name="indonesia"></a>
## Indonesia

**v3.0.2** — Library TCP & Protobuf otomatis latar belakang yang thread-safe dan bebas dependensi eksternal untuk ESP32 dan ESP8266.  
Satu panggilan di `setup()`. Tidak ada kode di `loop()`. Tidak memerlukan pustaka eksternal.

### Cara Kerja

```
Koneksi WiFi            → menghubungkan WiFi, otomatis rekoneksi jika putus
TCP/Protobuf engine     → FreeRTOS task terdedikasi (ESP32) atau Ticker (ESP8266)
                          menjadi SATU-SATUNYA pemilik soket: connect, read,
                          write, keepalive, reconnect
Cache Lokal (Thread-safe) → menyimpan status parameter masuk, dapat dibaca instan
                          via getBool(), getDouble(), getString()
Outbox (Thread-safe)    → sendTelemetry() / sendBatch() dari konteks mana saja
                          menulis ke buffer — tanpa memblokir thread utama (non-blocking)
Heartbeat (8s)          → mengirim ping beserta fw_version secara otomatis
```

Soket tidak pernah diakses secara langsung dari konteks loop utama, mencegah terjadinya tabrakan soket (*socket race condition*). `void loop()` Anda tetap bersih dari kode boilerplate jaringan.

### Tanpa Dependensi Eksternal

Pustaka ini hanya menggunakan apa yang sudah terintegrasi di dalam SDK ESP32/ESP8266:

| Komponen | Sumber |
|---|---|
| TCP Protobuf engine | Terintegrasi (`ElectinsIoT` — ditulis dari awal dengan inline Protobuf) |
| TCP connection | `WiFiClient` (biasa) atau `WiFiClientSecure` (TLS) |
| Background engine | FreeRTOS task (ESP32) / async Ticker (ESP8266) |
| WiFi management | `WiFi` / `ESP8266WiFi` |

---

### Instalasi

1. Salin folder `ElectinsIoT` ke dalam direktori `libraries` Arduino Anda.
2. Restart IDE Anda.
3. Selesai — tidak ada pustaka lain yang perlu diinstal.

---

### Contoh Cepat

```cpp
#include <ElectinsIoT.h>

const char* WIFI_SSID    = "YOUR_SSID";
const char* WIFI_PASS    = "YOUR_PASSWORD";
const char* API_KEY      = "YOUR_API_KEY";
const char* FIRMWARE_VER = "1.0.0";

const char* PARAM_LED    = "led";
const char* PARAM_SUHU   = "suhu";

WiFiClient client;
ElectinsIoT iot(client);

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);

    iot.setDebug(true);

    // Otomatis mengurus Wi-Fi, TCP, heartbeat ping, & OTA update di latar belakang
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER);
}

void loop() {
    // 1. Membaca parameter dari cache lokal
    bool ledState = iot.getBool(PARAM_LED, false);
    if (ledState) {
        digitalWrite(LED_BUILTIN, HIGH);
    } else {
        digitalWrite(LED_BUILTIN, LOW);
    }

    // 2. Mengirim data telemetri setiap 3 detik
    static unsigned long lastSend = 0;
    if (millis() - lastSend >= 3000) {
        lastSend = millis();
        if (iot.connected()) {
            float temperature = 25.0f + random(0, 50) / 10.0f;
            iot.sendTelemetry(PARAM_SUHU, temperature);
        }
    }
}
```

---

### Referensi API

#### Inisialisasi & Koneksi Otomatis
- **`iot.beginWiFi(apiKey, ssid, pass, version = "1.0.0", useSsl = false, deviceId = nullptr)`**  
  Menginisialisasi dan memulai loop koneksi latar belakang otomatis. Menangani Wi-Fi, TCP, heartbeat, dan OTA. Jika `useSsl` diset `true`, otomatis terhubung ke port `8883` (wajib menggunakan objek `WiFiClientSecure` pada konstruktor). Jika `useSsl` diset `false`, otomatis terhubung ke port `1883` secara default.
- **`iot.connected()`**  
  Mengembalikan nilai `true` jika terhubung ke server.
- **`iot.disconnect()`**  
  Memutus koneksi soket TCP.

#### Polling Parameter Lokal (GET)
- **`iot.getBool(param, defaultValue = false)`**  
  Membaca status boolean parameter.
- **`iot.getDouble(param, defaultValue = 0.0)`**  
  Membaca nilai desimal parameter.
- **`iot.getString(param, defaultValue = "")`**  
  Membaca string parameter (pointer aman dari perubahan task latar belakang).

#### Pengiriman Telemetri (SET)
- **`iot.sendTelemetry(param, value)`** - Mengirim parameter angka tunggal.
- **`iot.sendTelemetryString(param, value)`** - Mengirim parameter teks/string tunggal.
- **`iot.sendTelemetryBool(param, value)`** - Mengirim parameter boolean tunggal.

#### Telemetri Batch (Banyak Parameter dalam Satu Paket)
- **`iot.startBatch()`** - Memulai antrean data batch baru.
- **`iot.addBatch(param, value)`** - Menambahkan data angka ke batch.
- **`iot.addBatchString(param, value)`** - Menambahkan data teks ke batch.
- **`iot.addBatchBool(param, value)`** - Menambahkan data boolean ke batch.
- **`iot.sendBatch()`** - Mengompilasi dan mengirim seluruh data batch.

#### Callbacks
- **`iot.onUpdateParam(cb)`**  
  `void cb(const char* param, double value, const char* stringValue)` - Dipanggil ketika ada perubahan parameter dari server.
- **`iot.onOtaUpdate(cb)`**  
  `void cb(const char* firmwareUrl)` - Dipanggil ketika ada instruksi update firmware OTA.
- **`iot.onReboot(cb)`**  
  `void cb()` - Dipanggil sesaat sebelum perangkat melakukan reboot `ESP.restart()`.

#### Konfigurasi
- **`iot.setDebug(enable)`** - Mengaktifkan atau menonaktifkan cetakan debug Serial.
- **`iot.setKeepAlive(seconds)`** - Menyetel interval heartbeat ping (default adalah 8s).

---

### Sambungan Aman / TLS (Port 8883)

Untuk koneksi aman, konfigurasikan dan masukkan objek `WiFiClientSecure` ke dalam konstruktor:

```cpp
#include <WiFiClientSecure.h>

WiFiClientSecure secureClient;
ElectinsIoT iot(secureClient);

void setup() {
    // Pasang sertifikat root CA ke WiFiClientSecure
    secureClient.setCACert(ROOT_CA_CERTIFICATE); // Atau gunakan setInsecure() untuk uji coba
    
    // Otomatis mengurus Wi-Fi dan koneksi TLS TCP di latar belakang
    iot.beginWiFi(API_KEY, WIFI_SSID, WIFI_PASS, FIRMWARE_VER, true);
}
```

---

Made with ❤️ by [Nash](mailto:support@electins.id) — [electins.id](https://electins.id)
