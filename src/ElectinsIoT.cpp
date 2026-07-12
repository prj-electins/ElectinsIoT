#include "ElectinsIoT.h"
#include <stdio.h>
#include <string.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <Update.h>
  #include <WiFiClientSecure.h>
  #include <esp_ota_ops.h>
  #include <freertos/semphr.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiClientSecure.h>
  #include <ESP8266HTTPClient.h>
  #include <ESP8266httpUpdate.h>
  #include <Ticker.h>
#endif

// Protobuf wire helper functions
static size_t writeVarint(uint8_t* buf, uint64_t val) {
    size_t len = 0;
    while (val >= 0x80) {
        buf[len++] = (uint8_t)((val & 0x7F) | 0x80);
        val >>= 7;
    }
    buf[len++] = (uint8_t)(val & 0x7F);
    return len;
}

static bool parseVarint(const uint8_t* buf, size_t maxLen, size_t& offset, uint64_t& val) {
    val = 0;
    size_t shift = 0;
    while (offset < maxLen) {
        uint8_t b = buf[offset++];
        val |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
        if (shift >= 64) return false;
    }
    return false;
}

struct DecodedCommand {
    int type = 0;
    char target_param[64] = {0};
    double value = 0.0;
    char metadata[256] = {0};
    char string_value[256] = {0};
};

static bool decodeCommand(const uint8_t* buf, size_t len, DecodedCommand& out) {
    size_t offset = 0;
    while (offset < len) {
        uint64_t tag = 0;
        if (!parseVarint(buf, len, offset, tag)) return false;
        uint32_t field = tag >> 3;
        uint32_t wire = tag & 7;

        if (wire == 0) { // Varint
            uint64_t val = 0;
            if (!parseVarint(buf, len, offset, val)) return false;
            if (field == 1) out.type = (int)val;
        } else if (wire == 1) { // 64-bit (double)
            if (len - offset < 8) return false;
            double val;
            memcpy(&val, &buf[offset], 8);
            offset += 8;
            if (field == 3) out.value = val;
        } else if (wire == 2) { // Length-delimited (string)
            uint64_t strLen = 0;
            if (!parseVarint(buf, len, offset, strLen)) return false;
            if (strLen > len - offset) return false;

            if (field == 2) {
                size_t cpy = strLen < sizeof(out.target_param) - 1 ? strLen : sizeof(out.target_param) - 1;
                memcpy(out.target_param, &buf[offset], cpy);
                out.target_param[cpy] = '\0';
            } else if (field == 4) {
                size_t cpy = strLen < sizeof(out.metadata) - 1 ? strLen : sizeof(out.metadata) - 1;
                memcpy(out.metadata, &buf[offset], cpy);
                out.metadata[cpy] = '\0';
            } else if (field == 5) {
                size_t cpy = strLen < sizeof(out.string_value) - 1 ? strLen : sizeof(out.string_value) - 1;
                memcpy(out.string_value, &buf[offset], cpy);
                out.string_value[cpy] = '\0';
            }
            offset += strLen;
        } else {
            if (wire == 5) {
                if (len - offset < 4) return false;
                offset += 4;
            } else {
                return false;
            }
        }
    }
    return true;
}

#if defined(ESP32)
static void electinsTask(void* pvParameters) {
    ElectinsIoT* iot = (ElectinsIoT*)pvParameters;
    while (true) {
        iot->loop();
        vTaskDelay(1); // Jeda 1 tick (1ms) untuk respons tercepat, tetap memberi ruang bagi OS/WiFi task
    }
}
#endif

#if defined(ESP8266)
static Ticker otaLoopTicker;
static void onTickerLoop(ElectinsIoT* iot) {
    iot->loop();
}
#endif

ElectinsIoT::ElectinsIoT(Client& client) : _client(client) {
    _otaInProgress = false;
    startBatch();
    _cacheDoubleCount = 0;
    _cacheStringCount = 0;
    _frameStartRef = 0;
    _lastRxTime = 0;
#if defined(ESP32)
    _mutex = xSemaphoreCreateMutex();
#endif
}

ElectinsIoT::~ElectinsIoT() {
#if defined(ESP32)
    if (_mutex) {
        vSemaphoreDelete((SemaphoreHandle_t)_mutex);
        _mutex = nullptr;
    }
#endif
}

void ElectinsIoT::lock() {
#if defined(ESP32)
    if (_mutex) {
        xSemaphoreTake((SemaphoreHandle_t)_mutex, portMAX_DELAY);
    }
#endif
}

void ElectinsIoT::unlock() {
#if defined(ESP32)
    if (_mutex) {
        xSemaphoreGive((SemaphoreHandle_t)_mutex);
    }
#endif
}

void ElectinsIoT::begin(const char* apiKey, const char* version, const char* deviceId) {
    lock();
    _apiKey = apiKey;
    
    if (version && version[0] != '\0') {
        strncpy(_version, version, sizeof(_version) - 1);
        _version[sizeof(_version) - 1] = '\0';
    } else {
        strcpy(_version, "1.0.0");
    }
    
    if (!deviceId || deviceId[0] == '\0') {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(_deviceId, sizeof(_deviceId), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        strncpy(_deviceId, deviceId, sizeof(_deviceId) - 1);
        _deviceId[sizeof(_deviceId) - 1] = '\0';
    }
    _appValidated = false;
    unlock();
}

void ElectinsIoT::beginWiFi(const char* apiKey, const char* ssid, const char* pass, 
                           const char* version, bool useSsl, const char* deviceId) {
    _ssid = ssid;
    _pass = pass;
    
    // Atur port default jika masih mengarah ke server produksi awan
    if (strcmp(_host, "iot.electins.id") == 0) {
        _port = useSsl ? 8883 : 1883;
    }

    begin(apiKey, version, deviceId);

    _log("[WiFi] Configured automatic background network loop.");

#if defined(ESP32)
    // Jalankan task latar belakang pada Core 1
    xTaskCreatePinnedToCore(electinsTask, "ElectinsTask", 4096, this, 1, NULL, 1);
#elif defined(ESP8266)
    // Jalankan ticker asinkron setiap 20ms
    otaLoopTicker.attach_ms(20, onTickerLoop, this);
#endif
}

void ElectinsIoT::setLocalServer(const char* host, uint16_t port) {
    lock();
    _host = host;
    _port = port;
    unlock();
}

bool ElectinsIoT::connect(const char* host, uint16_t port) {
    lock();
    if (host != nullptr && host[0] != '\0') {
        _host = host;
    }
    if (port != 0) {
        _port = port;
    }
    const char* targetHost = _host;
    uint16_t targetPort = _port;
    _log("[TCP] Connecting to host: ", targetHost);
    
    _client.stop(); // Bersihkan koneksi usang
    unlock(); // Lepas lock sebelum melakukan pemanggilan socket connect yang bersifat blocking

    bool success = _client.connect(targetHost, targetPort);
    
    lock();
    if (success) {
        _lenBytesRead = 0;
        _rxMsgLen = 0;
        _rxMsgRead = 0;
        _lastPingTime = millis();
        _lastRxTime = millis(); // Catat waktu penerimaan awal
        _log("[TCP] Connection successful");
        
        // Kirim ping autentikasi awal secara instan agar server langsung memetakan device ID
        sendPing();
        
        unlock();
        return true;
    }
    _log("[TCP] Connection failed");
    unlock();
    return false;
}

bool ElectinsIoT::connected() {
    lock();
    if (_otaInProgress) {
        unlock();
        return false;
    }
    bool conn = _client.connected();
    unlock();
    return conn;
}

void ElectinsIoT::disconnect() {
    lock();
    _log("[TCP] Manual disconnect executed");
    _client.stop();
    unlock();
}

void ElectinsIoT::loop() {
    lock();
    if (_otaInProgress) {
        unlock();
        return;
    }
    unlock();

    // ─── Otomatisasi WiFi dan TCP Reconnection ─────────────────────────────
    if (_ssid != nullptr && _ssid[0] != '\0') {
        uint8_t currentWifiStatus = WiFi.status();
        bool justConnected = (currentWifiStatus == WL_CONNECTED && _lastWifiStatus != WL_CONNECTED);
        _lastWifiStatus = currentWifiStatus;

        if (currentWifiStatus != WL_CONNECTED) {
            lock();
            _client.stop(); // Pastikan soket ditutup jika WiFi putus
            unlock();
            
            unsigned long now = millis();
            if (_lastWifiAttempt == 0 || now - _lastWifiAttempt > 10000) {
                _lastWifiAttempt = now;
                _log("[WiFi] Connecting to SSID: ", _ssid);
                WiFi.begin(_ssid, _pass);
            }
            return; // Tunggu sampai WiFi terhubung sebelum melanjutkan loop
        }
        
        // WiFi sudah terhubung, cek koneksi TCP
        lock();
        bool isTcpConnected = _client.connected();
        unlock();
        
        if (!isTcpConnected) {
            unsigned long now = millis();
            // Jika WiFi baru saja tersambung (justConnected), langsung hubungkan TCP tanpa menunggu jeda 5 detik
            if (justConnected || _lastTcpAttempt == 0 || now - _lastTcpAttempt > 5000) {
                _lastTcpAttempt = now;
                _log("[TCP] Connecting to server: ", _host);
                connect(_host, _port);
            }
            return; // Tunggu koneksi TCP terbentuk
        }
    }

    lock();
    if (!_client.connected()) {
        unlock();
        return;
    }

    // Proteksi Frame Receive Timeout: jika pembacaan frame menggantung > 2 detik, putus koneksi untuk sinkronisasi ulang stream TCP
    if (_lenBytesRead > 0 && millis() - _frameStartRef > 2000) {
        _log("[TCP] Frame receive timeout, forcing disconnect");
        _client.stop();
        _lenBytesRead = 0;
        _rxMsgLen = 0;
        _rxMsgRead = 0;
        unlock();
        return;
    }

    // Proteksi TCP Half-Open Connection Timeout: jika koneksi aktif tapi tidak menerima data > 24s, putus koneksi
    if (_lastRxTime > 0 && millis() - _lastRxTime > 24000) {
        _log("[TCP] No data received for 24s (half-open connection), forcing disconnect");
        _client.stop();
        _lenBytesRead = 0;
        _rxMsgLen = 0;
        _rxMsgRead = 0;
        unlock();
        return;
    }

    // Send keepalive ping periodically (default 8s)
    if (millis() - _lastPingTime > _pingIntervalMs) {
        sendPing();
    }

    // Read socket
    while (_client.available() > 0) {
        if (_lenBytesRead < 4) {
            int b = _client.read();
            if (b < 0) break;
            if (_lenBytesRead == 0) {
                _frameStartRef = millis(); // Catat waktu awal pembacaan frame baru
            }
            _lenBuf[_lenBytesRead++] = (uint8_t)b;
            if (_lenBytesRead == 4) {
                _rxMsgLen = ((uint32_t)_lenBuf[0] << 24) |
                            ((uint32_t)_lenBuf[1] << 16) |
                            ((uint32_t)_lenBuf[2] << 8)  |
                             (uint32_t)_lenBuf[3];
                _rxMsgRead = 0;
                
                if (_rxMsgLen > sizeof(_rxBuffer)) {
                    _log("[TCP] Message too large: ", String(_rxMsgLen).c_str());
                    _client.stop();
                    _lenBytesRead = 0;
                    _rxMsgLen = 0;
                    _rxMsgRead = 0;
                    unlock();
                    return;
                }
                
                if (_rxMsgLen == 0) {
                    unlock(); // Lepas lock sebelum memanggil callback/pemrosesan
                    handleIncomingFrame(_rxBuffer, 0);
                    lock(); // Kunci kembali
                    _lenBytesRead = 0;
                    _rxMsgLen = 0;
                    _rxMsgRead = 0;
                    continue;
                }
            }
            continue;
        }

        size_t avail = _client.available();
        size_t toRead = _rxMsgLen - _rxMsgRead;
        if (toRead > avail) {
            toRead = avail;
        }
        if (toRead > 0) {
            int readBytes = _client.read(&_rxBuffer[_rxMsgRead], toRead);
            if (readBytes <= 0) break;
            _rxMsgRead += readBytes;
            
            if (_rxMsgRead == _rxMsgLen) {
                unlock(); // Lepas lock sebelum memanggil callback/pemrosesan
                handleIncomingFrame(_rxBuffer, _rxMsgLen);
                lock(); // Kunci kembali
                _lenBytesRead = 0;
                _rxMsgLen = 0;
                _rxMsgRead = 0;
            }
        }
    }
    unlock();
}

bool ElectinsIoT::writeFrame(const uint8_t* pbData, size_t pbSize) {
    if (!_client.connected()) return false;

    // Write 4-byte big-endian header
    uint8_t header[4];
    header[0] = (pbSize >> 24) & 0xFF;
    header[1] = (pbSize >> 16) & 0xFF;
    header[2] = (pbSize >> 8) & 0xFF;
    header[3] = pbSize & 0xFF;

    _client.write(header, 4);
    _client.write(pbData, pbSize);
    return true;
}

void ElectinsIoT::handleIncomingFrame(const uint8_t* pbData, size_t pbSize) {
    DecodedCommand cmd;
    if (decodeCommand(pbData, pbSize, cmd)) {
        _lastRxTime = millis(); // Perbarui waktu terakhir data diterima
        if (cmd.type == 0) { // PING
            // Silent ping
        } 
        else if (cmd.type == 1) { // UPDATE_PARAM
            _log("[CMD] Received UPDATE_PARAM for: ", (String(cmd.target_param) + " = " + String(cmd.value) + " (Text: " + cmd.string_value + ")").c_str());
            updateCacheDouble(cmd.target_param, cmd.value);
            updateCacheString(cmd.target_param, cmd.string_value);
            if (_updateParamCb) {
                _updateParamCb(cmd.target_param, cmd.value, cmd.string_value);
            }
        } 
        else if (cmd.type == 2) { // REBOOT
            _log("[CMD] Received REBOOT command");
            if (_rebootCb) {
                _rebootCb();
            }
            _client.stop();
            delay(100);
            ESP.restart();
        } 
        else if (cmd.type == 3) { // OTA_UPDATE
            _log("[CMD] Received OTA_UPDATE command: ", cmd.metadata);
            if (_otaUpdateCb) {
                _otaUpdateCb(cmd.metadata);
            }
            performOtaUpdate(cmd.metadata);
        }
    } else {
        _log("[TCP] Protobuf command decode failed");
    }
}

void ElectinsIoT::sendPing() {
    _lastPingTime = millis();
    const char* sKeys[1] = { "fw_version" };
    const char* sVals[1] = { _version };
    _sendTelemetry(nullptr, nullptr, 0, sKeys, sVals, 1);
}

bool ElectinsIoT::sendTelemetry(const char* param, double value) {
    lock();
    if (!param) { unlock(); return false; }
    const char* dKeys[1] = { param };
    double dVals[1] = { value };
    bool ok = _sendTelemetry(dKeys, dVals, 1, nullptr, nullptr, 0);
    if (ok) _log("[Telemetry] Sent parameter: ", param);
    unlock();
    return ok;
}

bool ElectinsIoT::sendTelemetryString(const char* param, const char* value) {
    lock();
    if (!param || !value) { unlock(); return false; }
    const char* sKeys[1] = { param };
    const char* sVals[1] = { value };
    bool ok = _sendTelemetry(nullptr, nullptr, 0, sKeys, sVals, 1);
    if (ok) _log("[Telemetry] Sent string parameter: ", param);
    unlock();
    return ok;
}

void ElectinsIoT::startBatch() {
    _batchDoubleCount = 0;
    _batchStringCount = 0;
}

void ElectinsIoT::addBatch(const char* param, double value) {
    if (!param || _batchDoubleCount >= ELECTINS_MAX_BATCH) return;
    
    strncpy(_batchDoubleKeys[_batchDoubleCount], param, sizeof(_batchDoubleKeys[0]) - 1);
    _batchDoubleKeys[_batchDoubleCount][sizeof(_batchDoubleKeys[0]) - 1] = '\0';
    _batchDoubleVals[_batchDoubleCount] = value;
    _batchDoubleCount++;
}

void ElectinsIoT::addBatchString(const char* param, const char* value) {
    if (!param || !value || _batchStringCount >= ELECTINS_MAX_BATCH) return;
    
    strncpy(_batchStringKeys[_batchStringCount], param, sizeof(_batchStringKeys[0]) - 1);
    _batchStringKeys[_batchStringCount][sizeof(_batchStringKeys[0]) - 1] = '\0';
    strncpy(_batchStringVals[_batchStringCount], value, sizeof(_batchStringVals[0]) - 1);
    _batchStringVals[_batchStringCount][sizeof(_batchStringVals[0]) - 1] = '\0';
    _batchStringCount++;
}

bool ElectinsIoT::sendBatch() {
    lock();
    const char* doubleKeys[ELECTINS_MAX_BATCH];
    for (uint8_t i = 0; i < _batchDoubleCount; i++) {
        doubleKeys[i] = _batchDoubleKeys[i];
    }
    const char* stringKeys[ELECTINS_MAX_BATCH];
    const char* stringVals[ELECTINS_MAX_BATCH];
    for (uint8_t i = 0; i < _batchStringCount; i++) {
        stringKeys[i] = _batchStringKeys[i];
        stringVals[i] = _batchStringVals[i];
    }
    
    bool ok = _sendTelemetry(doubleKeys, _batchDoubleVals, _batchDoubleCount, 
                             stringKeys, stringVals, _batchStringCount);
    
    if (ok) {
        _log("[Telemetry] Sent batch data package");
    }
    startBatch(); // Reset batch after send
    unlock();
    return ok;
}

void ElectinsIoT::onUpdateParam(UpdateParamCallback cb) { _updateParamCb = cb; }
void ElectinsIoT::onOtaUpdate(OtaUpdateCallback cb)     { _otaUpdateCb     = cb; }
void ElectinsIoT::onReboot(RebootCallback cb)           { _rebootCb        = cb; }

void ElectinsIoT::setDebug(bool enable) {
    _debug = enable;
}

void ElectinsIoT::setKeepAlive(uint16_t seconds) {
    _pingIntervalMs = seconds > 0 ? (unsigned long)seconds * 1000UL : 8000UL;
}

bool ElectinsIoT::_sendTelemetry(const char* const* doubleKeys, const double* doubleVals, size_t doubleCount,
                                 const char* const* stringKeys, const char* const* stringVals, size_t stringCount) {
    if (_otaInProgress) return false;
    if (!_client.connected()) return false;
    
    size_t offset = 0;

    // api_key (field 1)
    if (_apiKey && _apiKey[0] != '\0') {
        _txBuffer[offset++] = 0x0A;
        size_t len = strlen(_apiKey);
        offset += writeVarint(_txBuffer + offset, len);
        memcpy(_txBuffer + offset, _apiKey, len);
        offset += len;
    }

    // device_id (field 2)
    if (_deviceId && _deviceId[0] != '\0') {
        _txBuffer[offset++] = 0x12;
        size_t len = strlen(_deviceId);
        offset += writeVarint(_txBuffer + offset, len);
        memcpy(_txBuffer + offset, _deviceId, len);
        offset += len;
    }

    // parameters (field 4) - map<string, double>
    for (size_t i = 0; i < doubleCount; i++) {
        if (!doubleKeys[i]) continue;
        
        size_t keyLen = strlen(doubleKeys[i]);
        uint8_t keyVarintBuf[10];
        size_t keyVarintLen = writeVarint(keyVarintBuf, keyLen);
        
        size_t entryLen = 1 + keyVarintLen + keyLen + 1 + 8;
        
        if (offset + 1 + 10 + entryLen > sizeof(_txBuffer)) break; // Overflow protection
        
        _txBuffer[offset++] = 0x22;
        offset += writeVarint(_txBuffer + offset, entryLen);
        
        _txBuffer[offset++] = 0x0A;
        memcpy(_txBuffer + offset, keyVarintBuf, keyVarintLen);
        offset += keyVarintLen;
        memcpy(_txBuffer + offset, doubleKeys[i], keyLen);
        offset += keyLen;
        
        _txBuffer[offset++] = 0x11;
        double val = doubleVals[i];
        memcpy(_txBuffer + offset, &val, 8);
        offset += 8;
    }

    // string_parameters (field 5) - map<string, string>
    for (size_t i = 0; i < stringCount; i++) {
        if (!stringKeys[i] || !stringVals[i]) continue;
        
        size_t keyLen = strlen(stringKeys[i]);
        uint8_t keyVarintBuf[10];
        size_t keyVarintLen = writeVarint(keyVarintBuf, keyLen);
        
        size_t valLen = strlen(stringVals[i]);
        uint8_t valVarintBuf[10];
        size_t valVarintLen = writeVarint(valVarintBuf, valLen);
        
        size_t entryLen = 1 + keyVarintLen + keyLen + 1 + valVarintLen + valLen;
        
        if (offset + 1 + 10 + entryLen > sizeof(_txBuffer)) break; // Overflow protection
        
        _txBuffer[offset++] = 0x2A;
        offset += writeVarint(_txBuffer + offset, entryLen);
        
        _txBuffer[offset++] = 0x0A;
        memcpy(_txBuffer + offset, keyVarintBuf, keyVarintLen);
        offset += keyVarintLen;
        memcpy(_txBuffer + offset, stringKeys[i], keyLen);
        offset += keyLen;
        
        _txBuffer[offset++] = 0x12;
        memcpy(_txBuffer + offset, valVarintBuf, valVarintLen);
        offset += valVarintLen;
        memcpy(_txBuffer + offset, stringVals[i], valLen);
        offset += valLen;
    }

    bool success = writeFrame(_txBuffer, offset);
    if (success) {
#if defined(ESP32)
        if (!_appValidated) {
            _appValidated = true;
            esp_ota_mark_app_valid_cancel_rollback();
            _log("[OTA] App marked as valid, rollback cancelled.");
        }
#endif
    }
    return success;
}

void ElectinsIoT::_log(const char* msg, const char* val) {
    if (_debug) {
        if (val) {
            Serial.print(msg);
            Serial.println(val);
        } else {
            Serial.println(msg);
        }
    }
}

void ElectinsIoT::performOtaUpdate(const char* firmwareUrl) {
    lock();
    _otaInProgress = true;
    _client.stop(); // Putus koneksi utama agar bandwidth dan CPU 100% fokus ke OTA
    unlock();

    _log("[OTA] Starting internal firmware update from: ", firmwareUrl);
    
#if defined(ESP32)
    HTTPClient http;
    if (http.begin(firmwareUrl)) {
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            int contentLength = http.getSize();
            _log("[OTA] Content size to download: ", String(contentLength).c_str());
            
            if (contentLength <= 0) {
                _log("[OTA] Invalid content length, aborting OTA.");
                http.end();
                lock();
                _otaInProgress = false;
                unlock();
                return;
            }
            
            if (Update.begin(contentLength)) {
                _log("[OTA] Flashing in progress...");
                Stream* stream = http.getStreamPtr();
                size_t written = Update.writeStream(*stream);
                
                if (written == contentLength) {
                    _log("[OTA] Binary written successfully to partition.");
                } else {
                    _log("[OTA] Flashing failed. Bytes written: ", String(written).c_str());
                }
                
                if (Update.end()) {
                    if (Update.isFinished()) {
                        _log("[OTA] Update successful! Rebooting ESP32...");
                        _client.stop();
                        delay(500);
                        ESP.restart();
                    } else {
                        _log("[OTA] Update finished but not marked as completed.");
                    }
                } else {
                    _log("[OTA] Update finalization failed. Error: ", Update.errorString());
                }
            } else {
                _log("[OTA] Not enough partition space for flashing. Error: ", Update.errorString());
            }
        } else {
            _log("[OTA] HTTP GET failed, response code: ", String(httpCode).c_str());
        }
        http.end();
    } else {
        _log("[OTA] Failed to initialize HTTP client connection.");
    }
#elif defined(ESP8266)
    WiFiClient otaClient;
    t_httpUpdate_return ret = ESP8266httpUpdate.update(otaClient, firmwareUrl);
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            _log("[OTA] Update failed, error: ", ESP8266httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            _log("[OTA] No updates available.");
            break;
        case HTTP_UPDATE_OK:
            _log("[OTA] Update successful!");
            break;
    }
#endif

    // Reset jika proses OTA gagal/tidak melakukan restart
    lock();
    _otaInProgress = false;
    unlock();
}

double ElectinsIoT::getDouble(const char* param, double defaultValue) {
    if (param == nullptr || param[0] == '\0') return defaultValue;
    lock();
    for (uint8_t i = 0; i < _cacheDoubleCount; i++) {
        if (strcmp(_cacheDoubleKeys[i], param) == 0) {
            double val = _cacheDoubleVals[i];
            unlock();
            return val;
        }
    }
    unlock();
    return defaultValue;
}

const char* ElectinsIoT::getString(const char* param, const char* defaultValue) {
    if (param == nullptr || param[0] == '\0') return defaultValue;
    lock();
    for (uint8_t i = 0; i < _cacheStringCount; i++) {
        if (strcmp(_cacheStringKeys[i], param) == 0) {
            strncpy(_getStringBuf, _cacheStringVals[i], sizeof(_getStringBuf) - 1);
            _getStringBuf[sizeof(_getStringBuf) - 1] = '\0';
            unlock();
            return _getStringBuf;
        }
    }
    unlock();
    return defaultValue;
}

void ElectinsIoT::updateCacheDouble(const char* key, double val) {
    lock();

    for (uint8_t i = 0; i < _cacheDoubleCount; i++) {
        if (strcmp(_cacheDoubleKeys[i], key) == 0) {
            _cacheDoubleVals[i] = val;
            unlock();
            return;
        }
    }

    if (_cacheDoubleCount < ELECTINS_MAX_BATCH) {
        strncpy(_cacheDoubleKeys[_cacheDoubleCount], key, sizeof(_cacheDoubleKeys[0]) - 1);
        _cacheDoubleKeys[_cacheDoubleCount][sizeof(_cacheDoubleKeys[0]) - 1] = '\0';
        _cacheDoubleVals[_cacheDoubleCount] = val;
        _cacheDoubleCount++;
    }
    unlock();
}

void ElectinsIoT::updateCacheString(const char* key, const char* val) {
    lock();
    for (uint8_t i = 0; i < _cacheStringCount; i++) {
        if (strcmp(_cacheStringKeys[i], key) == 0) {
            strncpy(_cacheStringVals[i], val, sizeof(_cacheStringVals[0]) - 1);
            _cacheStringVals[i][sizeof(_cacheStringVals[0]) - 1] = '\0';
            unlock();
            return;
        }
    }
    if (_cacheStringCount < ELECTINS_MAX_BATCH) {
        strncpy(_cacheStringKeys[_cacheStringCount], key, sizeof(_cacheStringKeys[0]) - 1);
        _cacheStringKeys[_cacheStringCount][sizeof(_cacheStringKeys[0]) - 1] = '\0';
        strncpy(_cacheStringVals[_cacheStringCount], val, sizeof(_cacheStringVals[0]) - 1);
        _cacheStringVals[_cacheStringCount][sizeof(_cacheStringVals[0]) - 1] = '\0';
        _cacheStringCount++;
    }
    unlock();
}
bool ElectinsIoT::getBool(const char* param, bool defaultValue) {
    if (param == nullptr || param[0] == '\0') return defaultValue;
    lock();
    // Cari di cache double dulu
    for (uint8_t i = 0; i < _cacheDoubleCount; i++) {
        if (strcmp(_cacheDoubleKeys[i], param) == 0) {
            double val = _cacheDoubleVals[i];
            unlock();
            return val > 0.5;
        }
    }

    for (uint8_t i = 0; i < _cacheStringCount; i++) {
        if (strcmp(_cacheStringKeys[i], param) == 0) {
            const char* val = _cacheStringVals[i];
            bool res = defaultValue;
            if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0) {
                res = true;
            } else if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0) {
                res = false;
            }
            unlock();
            return res;
        }
    }
    unlock();
    return defaultValue;
}

bool ElectinsIoT::sendTelemetryBool(const char* param, bool value) {
    return sendTelemetry(param, value ? 1.0 : 0.0);
}

void ElectinsIoT::addBatchBool(const char* param, bool value) {
    addBatch(param, value ? 1.0 : 0.0);
}
