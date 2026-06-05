#pragma once

/*
 * ============================================================================
 * ElectinsMqtt.h — Internal MQTT 3.1.1 Engine
 * ============================================================================
 *
 * Layer TCP  : WiFiClient / WiFiClientSecure (built-in ESP32/ESP8266 SDK)
 * Polling    : dipanggil dari Ticker setiap ~10ms — BENAR-BENAR non-blocking
 * QoS        : 0 (fire-and-forget) dan 1 (acknowledged delivery)
 * Zero deps  : tidak memerlukan library eksternal apapun
 *
 * Desain non-blocking:
 *   - connect()     : blocking singkat saat CONNACK (hanya saat (re)connect,
 *                     dipanggil dari task terpisah via Ticker reconnect)
 *   - poll()        : 100% non-blocking — tidak ada delay(), tidak ada spin-wait
 *   - _decodeRemLen : baca hanya byte yang sudah tersedia, return false jika
 *                     belum cukup (tidak tunggu)
 *   - _handlePublish: baca hanya data yang sudah ada di buffer TCP
 *
 * File ini adalah internal library — tidak diakses langsung oleh pengguna.
 * ============================================================================
 */

#include <Arduino.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <WiFiClient.h>
  #include <WiFiClientSecure.h>
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

// Buffer MQTT
// RX_BUF_SIZE harus cukup untuk payload + 1 byte guard (null-terminator)
// Sehingga indeks [payloadStart + payloadLen] selalu valid
#define EMQTT_TX_BUF_SIZE    512
#define EMQTT_RX_BUF_SIZE    512
#define EMQTT_RX_GUARD_SIZE  (EMQTT_RX_BUF_SIZE + 1)  // +1 untuk null-terminator aman
#define EMQTT_MAX_TOPIC_LEN  128
#define EMQTT_TCP_TIMEOUT    5000  // ms — timeout koneksi TCP
#define EMQTT_CONNACK_WAIT   5000  // ms — timeout tunggu CONNACK

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

// ─── State machine state untuk _handlePublish (non-blocking read) ─────────────
enum EmqttReadState {
    EMQTT_READ_IDLE = 0,   // tidak ada paket yang sedang dibaca
    EMQTT_READ_PAYLOAD,    // sedang mengumpulkan byte payload
};

// ─── Kelas Engine MQTT ────────────────────────────────────────────────────────
class ElectinsMqtt {
public:
    ElectinsMqtt();
    ~ElectinsMqtt();

    // ── Konfigurasi ───────────────────────────────────────────────────────────
    void setServer(const char* host, uint16_t port);
    void setClientId(const char* clientId);
    void setCredentials(const char* user, const char* pass);
    void setKeepAlive(uint16_t seconds);
    void setWill(const char* topic, const char* payload,
                 bool retain = true, uint8_t qos = 0);
    void setSecure(bool enable);
    void setInsecure(bool insecure);

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // connect() — blocking saat CONNACK, max EMQTT_CONNACK_WAIT ms
    // Dipanggil dari reconnect Ticker (bukan dari _pollTicker)
    bool connect();
    void disconnect();
    bool connected();

    // ── Publish ───────────────────────────────────────────────────────────────
    uint16_t publish(const char* topic, const char* payload,
                     uint16_t payloadLen, bool retain = false, uint8_t qos = 0);

    // ── Subscribe / Unsubscribe ───────────────────────────────────────────────
    uint16_t subscribe(const char* topic, uint8_t qos = 0);
    uint16_t unsubscribe(const char* topic);

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void onConnect(EmqttConnectCb cb)       { _connectCb    = cb; }
    void onDisconnect(EmqttDisconnectCb cb) { _disconnectCb = cb; }
    void onMessage(EmqttMessageCb cb)       { _messageCb    = cb; }

    // ── Poll — dipanggil Ticker setiap ~10ms, HARUS non-blocking ─────────────
    void poll();

private:
    // ── TCP ───────────────────────────────────────────────────────────────────
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
    bool     _secure          = false;
    bool     _insecure        = false; // default false — verifikasi sertifikat aktif

    // ── State ─────────────────────────────────────────────────────────────────
    bool     _connected    = false;
    uint16_t _packetId     = 1;
    uint32_t _lastPingAt   = 0;
    bool     _pingPending  = false;
    uint32_t _pingSentAt   = 0;

    // ── Non-blocking partial read state ──────────────────────────────────────
    // Digunakan oleh poll() saat paket PUBLISH datang dalam beberapa Ticker cycle
    EmqttReadState _readState   = EMQTT_READ_IDLE;
    uint8_t        _partialHdr  = 0;
    uint32_t       _partialRem  = 0;
    uint32_t       _partialRead = 0; // bytes yang sudah dibaca ke _rxBuf

    // ── Buffer ────────────────────────────────────────────────────────────────
    uint8_t  _txBuf[EMQTT_TX_BUF_SIZE];
    uint8_t  _rxBuf[EMQTT_RX_GUARD_SIZE]; // +1 guard byte untuk null-terminator aman

    // ── QoS1 pending ─────────────────────────────────────────────────────────
    EmqttPendingAck _pending[EMQTT_PENDING_ACK_MAX];

    // ── Callbacks ─────────────────────────────────────────────────────────────
    EmqttConnectCb    _connectCb    = nullptr;
    EmqttDisconnectCb _disconnectCb = nullptr;
    EmqttMessageCb    _messageCb    = nullptr;

    // ── Encode helpers ────────────────────────────────────────────────────────
    uint16_t _encodeRemLen(uint8_t* buf, uint16_t pos, uint32_t len);
    uint16_t _writeStr(uint8_t* buf, uint16_t pos, const char* str);
    bool     _sendBuf(const uint8_t* data, uint16_t len);
    uint16_t _nextPacketId();

    // ── Paket keluar ──────────────────────────────────────────────────────────
    bool _sendConnect();
    bool _sendPingReq();
    bool _sendPubAck(uint16_t packetId);
    bool _sendDisconnect();

    // ── Paket masuk ───────────────────────────────────────────────────────────
    void _dispatchPublish();
    void _handlePubAck();
    void _handleSubAck(uint32_t remLen);
    void _handlePingResp();

    // ── QoS1 ──────────────────────────────────────────────────────────────────
    void _registerPending(uint16_t packetId);
    void _clearPending(uint16_t packetId);
    void _checkPendingAcks();

    void _onDisconnected();

    // ── State partial decode (untuk header + remLen) ──────────────────────────
    // Digunakan poll() agar decoding antar Ticker cycle bisa dilanjutkan
    bool     _hdrReady      = false;
    uint8_t  _hdrByte       = 0;
    bool     _remLenStarted = false;
    uint8_t  _remLenShift   = 0;
    uint32_t _remLenAcc     = 0;
    bool     _remLenDone    = false;
};
