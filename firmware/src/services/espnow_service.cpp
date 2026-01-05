#include "espnow_service.h"

EspNowService espNowService;

// Broadcast address
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void EspNowService::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("Last Packet Send Status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void EspNowService::begin() {
    WiFi.mode(WIFI_STA);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    
    esp_now_register_send_cb(onDataSent);
    
    // Register peer
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo)); // Zero-initialize
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA; // Explicitly set interface
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK){
        Serial.println("Failed to add peer");
        return;
    }
}

void EspNowService::broadcast(const TempSensorData& data) {
    Serial.println("=== Temp Sensor ===");
    Serial.printf("ID             : %d\n", data.id);
    Serial.printf("Name           : %s\n", data.name);
    Serial.printf("Temperature    : %.2f C\n", data.temperature);
    Serial.printf("Battery Voltage: %.2f V\n", data.batteryVoltage);
    Serial.printf("Battery Level  : %d %%\n", data.batteryLevel);
    Serial.println("===================");

    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &data, sizeof(data));

    if (result == ESP_OK) {
        Serial.println("Sent BROADCAST Success");
    } else {
        Serial.println("Error sending BROADCAST");
    }
}

void EspNowService::sendToPeer(const TempSensorData& data, const uint8_t* peerMac) {
    Serial.printf("=== Sending to Peer %02X:%02X:%02X:%02X:%02X:%02X ===\n", 
                  peerMac[0], peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5]);
    
    // Restore detailed data logging as requested
    Serial.println("=== Temp Sensor ===");
    Serial.printf("ID             : %d\n", data.id);
    Serial.printf("Name           : %s\n", data.name);
    Serial.printf("Temperature    : %.2f C\n", data.temperature);
    Serial.printf("Battery Voltage: %.2f V\n", data.batteryVoltage);
    Serial.printf("Battery Level  : %d %%\n", data.batteryLevel);
    Serial.printf("Interval       : %d ms\n", data.updateInterval);
    Serial.println("===================");

    esp_err_t result = esp_now_send(peerMac, (uint8_t *) &data, sizeof(data));
    
    if (result == ESP_OK) {
        Serial.println("Sent UNICAST Success");
    } else {
        Serial.printf("Error sending UNICAST: %d\n", result);
    }
}

// Helper to convert hex string to byte array
void hexToBytes(const char* hex, uint8_t* bytes, int len) {
    for (int i = 0; i < len; i++) {
        char buf[3] = { hex[i*2], hex[i*2+1], '\0' };
        bytes[i] = (uint8_t)strtoul(buf, NULL, 16);
    }
}

// Helper to parse MAC string
void parseMac(const char* macStr, uint8_t* macBytes) {
    int val[6];
    sscanf(macStr, "%x:%x:%x:%x:%x:%x", &val[0], &val[1], &val[2], &val[3], &val[4], &val[5]);
    for(int i=0; i<6; i++) macBytes[i] = (uint8_t)val[i];
}

void EspNowService::addSecurePeer(const char* macStr, const char* keyStr) {
    uint8_t peerMac[6];
    parseMac(macStr, peerMac);
    
    // Check if exists
    if (esp_now_is_peer_exist(peerMac)) {
        esp_now_del_peer(peerMac);
    }
    
    uint8_t keyBytes[16];
    hexToBytes(keyStr, keyBytes, 16);
    
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, peerMac, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = true;
    memcpy(peerInfo.lmk, keyBytes, 16);
    peerInfo.ifidx = WIFI_IF_STA;
    
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        Serial.printf("Secure Peer Added: %s\n", macStr);
    } else {
        Serial.println("Failed to Add Secure Peer");
    }
}
