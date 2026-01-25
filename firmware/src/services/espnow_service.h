#ifndef ESPNOW_SERVICE_H
#define ESPNOW_SERVICE_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

struct TempSensorData {
    uint8_t id; // 22 for Temp Sensor
    float temperature;
    float batteryVoltage;
    uint8_t batteryLevel;
    uint32_t updateInterval; // Expected time until next packet (ms)
    char name[16];
} __attribute__((packed));

class EspNowService {
public:
    void begin();
    void broadcast(const TempSensorData& data);
    void sendToPeer(const TempSensorData& data, const uint8_t* peerMac);
    void addSecurePeer(const char* macStr, const char* keyStr);
    
    void setForceBroadcast(bool force) { m_forceBroadcast = force; }
    bool isForceBroadcast() { return m_forceBroadcast; }
    
    volatile bool sendFinished = false;
    volatile bool sendSuccess = false;
    void resetSendStatus() { sendFinished = false; sendSuccess = false; }

private:
    static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
    bool m_forceBroadcast = false;
};

extern EspNowService espNowService;

#endif // ESPNOW_SERVICE_H
