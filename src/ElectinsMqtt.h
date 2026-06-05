#pragma once

/*
 * ============================================================================
 * ElectinsMqtt.h — Internal MQTT 3.1.1 Engine (v2.1.1)
 * ============================================================================
 *
 * Layer TCP  : WiFiClient / WiFiClientSecure (built-in ESP32/ESP8266 SDK)
 * Zero deps  : tidak memerlukan library eksternal apapun
 * QoS        : 0 (fire-and-forget) dan 1 (acknowledged delivery)
 *
 * Model eksekusi (single-owner) — desain ini menghilangkan race condition
 * yang menjadi penyebab restart pada versi sebelumnya:
 *
 *   - SATU pemilik socket: semua operasi socket (connect, read, write,
 *     keepalive, reconnect) HANYA terjadi di dalam loop() yang dipanggil
 *     dari satu konteks saja (task khusus di ESP32, scheduled function di
 *     ESP8266). Tidak ada lagi akses socket dari Ticker maupun WiFi-event.
 *
 *   - OUTBOX (antrian byte): publish()/subscribe()/unsubscribe() boleh
 *     dipanggil dari konteks mana pun. Mereka TIDAK menyentuh socket —
 *     hanya menaruh paket terenkode ke ring buffer (dilindungi mutex).
 *     loop() adalah satu-satunya yang menguras outbox ke socket.
 *
 *   - connected() hanya membaca flag state (atomic) — tidak menyentuh socket,
 *     jadi aman dipanggil dari konteks pengguna.
 *
 * File ini adalah internal library — tidak diakses langsung oleh pengguna.
 * ============================================================================
 */

#include <Arduino.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <WiFiClient.h>
  #include <WiFiClientSecure.h>
  #include <freertos/FreeRTOS.h>
  #include <freertos/semphr.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiClient.h>
  #include <WiFiClientSecure.h>
#endif

// ─── Konstanta MQTT 3.1.1 ────────────────────────────────────────────────────
#define EMQTT_CONNECT      0x10
#define EMQTT_CONNACK      0x20
#define EMQTT_PUBLISH      0x30
#define EMQTT_PUBACK       0x40
#define EMQTT_SUBSCRIBE    0x82
#define EMQTT_SUBACK       0x90
#define EMQTT_UNSUBSCRIBE  0xA2
#define EMQTT_UNSUBACK     0xB0
#define EMQTT_PINGREQ      0xC0
#define EMQTT_PINGRESP     0xD0
#define EMQTT_DISCONNECT   0xE0

// Kode return CONNACK
#define EMQTT_CONN_ACCEPTED  0x00

// ─── Buffer & timing ──────────────────────────────────────────────────────────
#define EMQTT_TX_SCRATCH_SIZE 512    // buffer enkode sementara (di bawah mutex)
#define EMQTT_RX_BUF_SIZE     512    // payload masuk maksimum
#define EMQTT_RX_GUARD_SIZE   (EMQTT_RX_BUF_SIZE + 1) // +1 null-terminator aman
#define EMQTT_MAX_TOPIC_LEN   128

#if defined(ESP8266)
  #define EMQTT_OUTBOX_SIZE   1024   // ring buffer outbox (RAM lebih kecil)
#else
  #define EMQTT_OUTBOX_SIZE   2048
#endif

#define EMQTT_TCP_TIMEOUT     5000   // ms — timeout koneksi TCP
#define EMQTT_CONNACK_WAIT    5000   // ms — timeout tunggu CONNACK
#define EMQTT_PING_TIMEOUT_MS 10000  // ms — disconnect jika PINGRESP tak datang
#define EMQTT_ACK_EXPIRE_MS   15000  // ms — hapus pending ACK lama (clean session)

// ─── Callback types (internal) ───────────────────────────────────────────────
typedef void (*EmqttConnectCb)();
typedef void (*EmqttDisconnectCb)();
typedef void (*EmqttMessageCb)(const char* topic, const char* payload,
                               uint16_t length, uint8_t qos, bool retain);

// ─── QoS1 pending ACK ────────────────────────────────────────────────────────
struct EmqttPendingAck {
    uint16_t packetId;
    uint32_t sentAt;
    bool     active;
};
#define EMQTT_PENDING_ACK_MAX 4

// ─── State koneksi ───────────────────────────────────────────────────────────
enum EmqttState {
    EMQTT_DISCONNECTED = 0,
    EMQTT_CONNECTED,
};

// ─── Kelas Engine MQTT ────────────────────────────────────────────────────────
class ElectinsMqtt {
public:
    ElectinsMqtt();
    ~ElectinsMqtt();

    // ── Inisialisasi (buat mutex) — panggil sekali sebelum loop() dijalankan ──
    void begin();

    // ── Konfigurasi ───────────────────────────────────────────────────────────
    void setServer(const char* host, uint16_t port);
    void setClientId(const char* clientId);
    void setCredentials(const char* user, const char* pass);
    void setKeepAlive(uint16_t seconds);
    void setReconnectInterval(uint16_t seconds);
    void setWill(const char* topic, const char* payload,
                 bool retain = true, uint8_t qos = 0);
    void setSecure(bool enable);
    void setInsecure(bool insecure);

    // ── Status (thread-safe — hanya baca flag) ────────────────────────────────
    bool connected() const { return _state == EMQTT_CONNECTED; }

    // ── Publish / Subscribe — enqueue ke outbox (thread-safe) ─────────────────
    uint16_t publish(const char* topic, const char* payload,
                     uint16_t payloadLen, bool retain = false, uint8_t qos = 0);
    uint16_t subscribe(const char* topic, uint8_t qos = 0);
    uint16_t unsubscribe(const char* topic);

    // ── Minta disconnect (thread-safe — diproses di loop) ─────────────────────
    void disconnect();

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void onConnect(EmqttConnectCb cb)       { _connectCb    = cb; }
    void onDisconnect(EmqttDisconnectCb cb) { _disconnectCb = cb; }
    void onMessage(EmqttMessageCb cb)       { _messageCb    = cb; }

    // ── Pump engine — HARUS dipanggil dari satu konteks pemilik saja ──────────
    // (task khusus di ESP32, scheduled function di ESP8266)
    void loop();

private:
    // ── TCP (hanya disentuh dari loop()) ──────────────────────────────────────
    WiFiClient*       _client    = nullptr;
    WiFiClientSecure* _secureCli = nullptr;
    Client*           _tcp       = nullptr;

    // ── Config ────────────────────────────────────────────────────────────────
    char     _host[64]        = {0};
    uint16_t _port            = 1883;
    char     _clientId[64]    = {0};
    char     _user[64]        = {0};
    char     _pass[64]        = {0};
    char     _willTopic[128]  = {0};
    char     _willPayload[64] = {0};
    bool     _willRetain      = true;
    uint8_t  _willQos         = 0;
    bool     _hasWill         = false;
    uint16_t _keepAliveSec    = 15;
    uint16_t _reconnectSec    = 5;
    bool     _secure          = false;
    bool     _insecure        = false;

    // ── State (volatile — ditulis loop(), dibaca konteks lain) ────────────────
    volatile EmqttState _state = EMQTT_DISCONNECTED;
    volatile bool _wantDisconnect = false;

    uint16_t _packetId      = 1;
    uint32_t _lastPingAt    = 0;
    bool     _pingPending   = false;
    uint32_t _pingSentAt    = 0;
    uint32_t _lastConnectAt = 0;
    bool     _firstAttempt  = true;

    // ── Outbox: ring buffer byte (producer dilindungi mutex) ──────────────────
    uint8_t           _outbox[EMQTT_OUTBOX_SIZE];
    volatile uint16_t _obHead = 0; // posisi tulis (producer)
    volatile uint16_t _obTail = 0; // posisi baca (consumer / loop)

    // ── Buffer enkode sementara (dipakai di bawah mutex) ──────────────────────
    uint8_t _txScratch[EMQTT_TX_SCRATCH_SIZE];

    // ── RX state machine (hanya dipakai loop()) ───────────────────────────────
    uint8_t  _rxBuf[EMQTT_RX_GUARD_SIZE];
    bool     _rxHeaderDone   = false;
    uint8_t  _rxHdr          = 0;
    bool     _rxRemLenDone   = false;
    uint32_t _rxRemLen       = 0;
    uint32_t _rxMultiplier   = 1;
    uint8_t  _rxRemLenBytes  = 0;
    uint32_t _rxPayloadRead  = 0;
    bool     _rxOversized    = false;

    // ── QoS1 pending ──────────────────────────────────────────────────────────
    EmqttPendingAck _pending[EMQTT_PENDING_ACK_MAX];

    // ── Callbacks ─────────────────────────────────────────────────────────────
    EmqttConnectCb    _connectCb    = nullptr;
    EmqttDisconnectCb _disconnectCb = nullptr;
    EmqttMessageCb    _messageCb    = nullptr;

    // ── Mutex (producer outbox + packetId + scratch) ──────────────────────────
#if defined(ESP32)
    SemaphoreHandle_t _mtx = nullptr;
#endif
    void _lock();
    void _unlock();

    // ── Lifecycle internal (loop context) ─────────────────────────────────────
    bool _attemptConnect();
    void _onDisconnected();
    void _drainOutbox();
    void _readIncoming();
    void _handleKeepAlive();
    void _resetRxState();

    // ── Outbox helpers ────────────────────────────────────────────────────────
    bool     _enqueue(const uint8_t* data, uint16_t len); // producer (locked)
    uint16_t _obCount() const;                            // bytes terpakai
    uint16_t _obFree() const;                             // bytes kosong

    // ── Enkode helpers ────────────────────────────────────────────────────────
    uint16_t _encodeRemLen(uint8_t* buf, uint16_t pos, uint32_t len);
    uint16_t _writeStr(uint8_t* buf, uint16_t pos, const char* str);
    uint16_t _nextPacketId();

    // ── Paket keluar (langsung dari loop context — kontrol kecil) ─────────────
    bool _sendConnect();
    bool _sendPingReq();
    bool _sendPubAck(uint16_t packetId);
    bool _sendDisconnect();
    bool _writeDirect(const uint8_t* data, uint16_t len);

    // ── Paket masuk (loop context) ────────────────────────────────────────────
    void _handlePacket(uint8_t hdr, uint32_t remLen);
    void _dispatchPublish(uint8_t hdr, uint32_t remLen);

    // ── QoS1 ──────────────────────────────────────────────────────────────────
    void _registerPending(uint16_t packetId);
    void _clearPending(uint16_t packetId);
    void _checkPendingAcks();
};
