#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <NimBLEDevice.h>

class BleService {
public:
    void begin(const char* deviceName);
    void updateTemperature(float temp);
    void updateBatteryLevel(int level);
    void updateName(const char* name);
    uint32_t getSleepInterval();
    bool isConnected();
    void setSleepInterval(uint32_t interval);
    void setNameCallback(std::function<void(const char*)> cb);
    void setPairedCallback(std::function<void(bool)> cb);
    void updatePaired(bool paired);
    bool isPaired();
    void setPaired(bool paired);
    void setPairingDataCallback(std::function<void(const char*)> cb);
    void startAdvertising();
    
    // Make callback accessible to friend class or just public helper
    std::function<void(const char*)> _pairingDataCallback;

private:
    static void onConnect(NimBLEServer* pServer);
    static void onDisconnect(NimBLEServer* pServer);
    
    NimBLEServer* _pServer;
    NimBLEService* _pService;
    NimBLECharacteristic* _pTempChar;
    NimBLECharacteristic* _pSleepChar;
    NimBLECharacteristic* _pBattChar;
    NimBLECharacteristic* _pNameChar;
    NimBLECharacteristic* _pPairedChar;
    
    std::function<void(const char*)> _nameCallback;
    std::function<void(bool)> _pairedCallback;
    
    bool _deviceConnected = false;
    uint32_t _sleepIntervalMs = 900000; // Default 15 mins
    bool _isPaired = false;
};

extern BleService bleService;

#endif // BLE_SERVICE_H
