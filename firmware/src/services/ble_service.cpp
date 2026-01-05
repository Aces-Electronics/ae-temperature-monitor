#include "ble_service.h"
#include <Arduino.h>
#include <WiFi.h>

// UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b" // Reuse Smart Shunt Service for now or defined new
#define TEMP_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a9" // Similar to Voltage but last digit 9
#define SLEEP_CHAR_UUID     "3A1B2C3D-4E5F-6A7B-8C9D-0E1F2A3B4C60" // Reuse disconnect delay or new?
// Actually let's use a specific one for Temp Sensor to distinguish?
// User said: "profiles for the BLE app so that it knows which device is connected"
// I will use a different Service UUID or just different characteristics.
// Let's stick to the Smart Shunt Service UUID for simplicity in discovery but specific characteristics.

#define TEMP_SENSOR_SERVICE_UUID "181A" // Environmental Sensing Service (Standard) or Custom?
// Let's use Custom to match user pattern.
#define AE_TEMP_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914c" // Ended with 'c'

#define CHAR_TEMP_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26aa"
#define CHAR_SLEEP_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26ab"
#define CHAR_BATT_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26ac"
#define CHAR_NAME_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26ad"
#define CHAR_PAIRED_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26ae"

BleService bleService;

class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        Serial.println("Client connected");
    };
    void onDisconnect(NimBLEServer* pServer) {
        Serial.println("Client disconnected");
    }
};

class PairedCallback: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            String valStr = String(value.c_str());
            Serial.printf("Paired Char Write: %s\n", valStr.c_str());
            
            if (valStr.startsWith("{")) {
                // JSON Payload -> Pairing Data
                // Let Main handle the parsing via callback
                if (bleService._pairingDataCallback) {
                     bleService._pairingDataCallback(valStr.c_str());
                }
            } else if (valStr == "RESET" || valStr == "UNPAIR") {
                // Reset Command
                 Serial.println("Received RESET command");
                 bleService.updatePaired(false);
            } else {
                 // Legacy bool/byte support (optional, or just ignore)
                 // uint8_t byteVal = (uint8_t)value[0];
                 // bool p = (byteVal != 0);
                 // bleService.updatePaired(p);
            }
        }
    }
};

class SleepCallback: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() == 4) {
             uint32_t interval = *(uint32_t*)value.data();
             bleService.setSleepInterval(interval);
             Serial.printf("New sleep interval: %d ms\n", interval);
        }
    }
};

class NameCallback: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
             // Assuming bleService has a public method orfriendship to trigger main callback
             // But we need to pass it back to main for NVS save? Or handle inside BleService if we pass Preferences?
             // Main handles NVS for simplicity as it has the Preferences object.
             // We need a callback mechanism.
             Serial.printf("New Name: %s\n", value.c_str());
             bleService.updateName(value.c_str()); // Helper to notify listener
        }
    }
};


// Helper to generate PIN from MAC (Matches Shunt)
uint32_t generatePinFromMac() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    // Use last 3 bytes to generate a unique 6-digit PIN
    uint32_t val = (mac[3] << 16) | (mac[4] << 8) | mac[5];
    uint32_t pin = val % 1000000;
    Serial.printf("[BLE SEC] PIN Code: %06d\n", pin);
    return pin;
}

void BleService::begin(const char* deviceName) {
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    
    // Security & Speed Configuration (Matches Shunt)
    uint32_t passkey = generatePinFromMac();
    NimBLEDevice::setSecurityAuth(true, true, true); // Bonding, MITM, Secure Connection
    NimBLEDevice::setSecurityPasskey(passkey);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY); // Forces user to enter PIN on phone
    
    _pServer = NimBLEDevice::createServer();
    _pServer->setCallbacks(new ServerCallbacks());
    
    _pService = _pServer->createService(AE_TEMP_SERVICE_UUID);
    
    _pTempChar = _pService->createCharacteristic(
        CHAR_TEMP_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC
    );
    
    _pSleepChar = _pService->createCharacteristic(
        CHAR_SLEEP_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC
    );
    _pSleepChar->setCallbacks(new SleepCallback());
    _pSleepChar->setValue((uint8_t*)&_sleepIntervalMs, 4);
    
    _pBattChar = _pService->createCharacteristic(
        CHAR_BATT_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC
    );
    
    
    _pNameChar = _pService->createCharacteristic(
        CHAR_NAME_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC
    );
    _pNameChar->setCallbacks(new NameCallback());
    _pNameChar->setValue(deviceName);

    // PAIRED Characteristic
    // 1. READ: Returns WiFi MAC Address (needed by App to match QR code target)
    // 2. WRITE: Accepts JSON payload {"gauge_mac": "...", "key": "..."} OR "RESET"
    _pPairedChar = _pService->createCharacteristic(
        CHAR_PAIRED_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC
    );
    // Set initial value to WiFi MAC Address
    String mac = WiFi.macAddress();
    _pPairedChar->setValue(mac.c_str());
    _pPairedChar->setCallbacks(new PairedCallback());

    _pService->start();

    
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(AE_TEMP_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    
    Serial.println("BLE Started (Secure)");
}

void BleService::updateTemperature(float temp) {
    if (_pTempChar) {
        _pTempChar->setValue(temp);
        _pTempChar->notify();
    }
}

void BleService::updateBatteryLevel(int level) {
    if (_pBattChar) {
        _pBattChar->setValue((uint32_t)level); // Simplification
        _pBattChar->notify();
    }
}

void BleService::updateName(const char* name) {
    if (_pNameChar) {
        // Explicitly construct string to ensure deep copy and length calculation
        std::string n(name);
        Serial.printf("DEBUG: BLE updating name char to: '%s' (len %d)\n", n.c_str(), n.length());
        _pNameChar->setValue(n);
        _pNameChar->notify();
    }
    if (_nameCallback) {
        _nameCallback(name);
    }
}

void BleService::updatePaired(bool paired) {
    _isPaired = paired;
    if (_pPairedChar) {
        uint8_t val = paired ? 1 : 0;
        _pPairedChar->setValue(&val, 1);
        _pPairedChar->notify();
    }
    if (_pairedCallback) {
        _pairedCallback(paired);
    }
}

void BleService::setNameCallback(std::function<void(const char*)> cb) {
    _nameCallback = cb;
}

void BleService::setPairedCallback(std::function<void(bool)> cb) {
    _pairedCallback = cb;
}

void BleService::setPairingDataCallback(std::function<void(const char*)> cb) {
    _pairingDataCallback = cb;
}

uint32_t BleService::getSleepInterval() {
    return _sleepIntervalMs;
}

void BleService::setSleepInterval(uint32_t interval) {
    _sleepIntervalMs = interval;
}

bool BleService::isConnected() {
    return _pServer->getConnectedCount() > 0;
}

bool BleService::isPaired() {
    return _isPaired;
}

void BleService::setPaired(bool paired) {
    _isPaired = paired;
}
