#pragma once

#include <Arduino.h>
#include <Client.h>

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

// Definisi Tipe Callback Command
typedef void (*UpdateParamCallback)(const char* param, double value, const char* stringValue);
typedef void (*OtaUpdateCallback)(const char* firmwareUrl);
typedef void (*RebootCallback)();

#define ELECTINS_MAX_BATCH 16

class ElectinsIoT {
public:
    ElectinsIoT(Client& client);
    ~ElectinsIoT();

    // Inisialisasi awal (manual)
    void begin(const char* apiKey, const char* version = "1.0.0", const char* deviceId = nullptr);
    
    // Inisialisasi otomatis + koneksi Wi-Fi & TCP otomatis latar belakang (eksklusif platform Electins)
    void beginWiFi(const char* apiKey, const char* ssid, const char* pass, 
                   const char* version = "1.0.0", bool useSsl = false, const char* deviceId = nullptr);

    // Kustomisasi server lokal untuk keperluan pengujian developer internal
    void setLocalServer(const char* host, uint16_t port = 1883);

    // Melakukan koneksi ke TCP Server
    bool connect(const char* host = nullptr, uint16_t port = 0);
    bool connected();
    void disconnect();

    // Loop pemeliharaan
    void loop();

    // Polling parameter dari cache lokal (GET)
    double getDouble(const char* param, double defaultValue = 0.0);
    const char* getString(const char* param, const char* defaultValue = "");
    bool getBool(const char* param, bool defaultValue = false);

    // Pengiriman Telemetri Tunggal (SET)
    bool sendTelemetry(const char* param, double value);
    bool sendTelemetryString(const char* param, const char* value);
    bool sendTelemetryBool(const char* param, bool value);

    // Pengiriman Telemetri Batch
    void startBatch();
    void addBatch(const char* param, double value);
    void addBatchString(const char* param, const char* value);
    void addBatchBool(const char* param, bool value);
    bool sendBatch();

    // Pengaturan Callback untuk Downstream Commands (opsional untuk pengguna tingkat lanjut)
    void onUpdateParam(UpdateParamCallback cb);
    void onOtaUpdate(OtaUpdateCallback cb);
    void onReboot(RebootCallback cb);

    // Konfigurasi
    void setDebug(bool enable);
    void setKeepAlive(uint16_t seconds);

    // Lock/Unlock untuk thread safety
    void lock();
    void unlock();

private:
    Client& _client;
    const char* _apiKey = nullptr;
    char _deviceId[64] = {0};
    bool _isCustomDeviceId = false;
    char _version[16] = {0}; // Menyimpan versi firmware aktif
    const char* _host = "iot.electins.id";
    uint16_t _port = 1883;
    
    // WiFi credentials untuk otomatisasi
    const char* _ssid = nullptr;
    const char* _pass = nullptr;
    unsigned long _lastWifiAttempt = 0;
    unsigned long _lastTcpAttempt = 0;
    uint8_t _lastWifiStatus = 255; // Menghindari status tak terdefinisi saat awal

    // Mutex untuk thread safety pada ESP32
#if defined(ESP32)
    void* _mutex = nullptr;
#endif

    // Callback penampung
    UpdateParamCallback _updateParamCb = nullptr;
    OtaUpdateCallback _otaUpdateCb = nullptr;
    RebootCallback _rebootCb = nullptr;

    // Buffer internal untuk framing & serialization
    uint8_t _txBuffer[1024];
    uint8_t _rxBuffer[1024];

    // Status pembacaan buffer TCP
    uint8_t  _lenBuf[4];
    uint8_t  _lenBytesRead = 0;
    uint32_t _rxMsgLen     = 0;
    uint32_t _rxMsgRead    = 0;

    // Data Batch
    char    _batchDoubleKeys[ELECTINS_MAX_BATCH][32];
    double  _batchDoubleVals[ELECTINS_MAX_BATCH];
    uint8_t _batchDoubleCount = 0;

    char    _batchStringKeys[ELECTINS_MAX_BATCH][32];
    char    _batchStringVals[ELECTINS_MAX_BATCH][64];
    uint8_t _batchStringCount = 0;

    // Cache parameter lokal untuk getDouble/getString
    char    _cacheDoubleKeys[ELECTINS_MAX_BATCH][32];
    double  _cacheDoubleVals[ELECTINS_MAX_BATCH];
    uint8_t _cacheDoubleCount = 0;

    char    _cacheStringKeys[ELECTINS_MAX_BATCH][32];
    char    _cacheStringVals[ELECTINS_MAX_BATCH][64];
    uint8_t _cacheStringCount = 0;


    unsigned long _frameStartRef = 0;
    unsigned long _lastRxTime = 0;

    bool _debug = false;
    bool _appValidated = false;
    bool _otaInProgress = false;
    unsigned long _lastPingTime = 0;
    unsigned long _pingIntervalMs = 8000; // Default 8 detik

    char _getStringBuf[64] = {0};

    // Helper serialization & framing
    bool writeFrame(const uint8_t* pbData, size_t pbSize);
    void handleIncomingFrame(const uint8_t* pbData, size_t pbSize);
    void sendPing();
    void performOtaUpdate(const char* firmwareUrl);

    void updateCacheDouble(const char* key, double val);
    void updateCacheString(const char* key, const char* val);


    bool _sendTelemetry(const char* const* doubleKeys, const double* doubleVals, size_t doubleCount,
                        const char* const* stringKeys, const char* const* stringVals, size_t stringCount);
    
    void _log(const char* msg, const char* val = nullptr);
    void ensureDeviceId();
};
