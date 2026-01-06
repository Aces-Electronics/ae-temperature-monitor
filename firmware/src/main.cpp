#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include "drivers/tmp102.h"
#include "drivers/neopixel.h"
#include "services/ble_service.h"
#include "services/espnow_service.h"
#include <nvs_flash.h>

// Pins
#define I2C_SDA 8
#define I2C_SCL 9
#define NEOPIXEL_PWR 5
#define NEOPIXEL_DATA 3

// Globals
TMP102 tmp102;
StatusLed statusLed(NEOPIXEL_PWR, NEOPIXEL_DATA);
Preferences preferences;

// Constants
#define DEFAULT_SLEEP_MS 900000 // 15 minutes
#define AWAKE_TIME_MS 30000     // Stay awake for 30s to allow connections

unsigned long stateStartTime = 0;
bool isStayingAwake = false;
uint8_t g_pairedMac[6] = {0}; // Global storage for paired MAC

void setup() {
    Serial.begin(115200);
    // Wait a bit for serial if USB connected
    delay(1000); 
    Serial.println("AE Temp Sensor Starting...");

    // Init NVS
    preferences.begin("ae-temp", false);
    uint32_t sleepInterval = preferences.getUInt("sleep_ms", DEFAULT_SLEEP_MS);
    
    // Load Name Suffix (previously "name")
    String nameSuffix = preferences.getString("name", ""); 
    Serial.printf("DEBUG: Loaded Name Suffix from NVS: '%s'\n", nameSuffix.c_str());
    
    String deviceName = "AE Temp Sensor";
    if (nameSuffix.length() > 0) {
        deviceName += " - " + nameSuffix;
    }
    Serial.printf("DEBUG: Full Device Name for BLE: '%s'\n", deviceName.c_str());
    
    bleService.setSleepInterval(sleepInterval);
    bleService.setNameCallback([](const char* suffix) {
        // Save only the suffix
        preferences.putString("name", suffix);
        Serial.printf("Saved new name suffix to NVS: %s\n", suffix);
    });

    bool isPaired = preferences.getBool("paired", false);
    bleService.setPaired(isPaired);
    bleService.setPairedCallback([](bool paired) {
        preferences.putBool("paired", paired);
        Serial.printf("Saved Paired Status to NVS: %s\n", paired ? "True" : "False");
        statusLed.flash(0, 128, 0, 500); // Green confirmation (Dimmed)
        
        if (!paired) {
            Serial.println("Factory Reset/Unpair Triggered!");
            // Visual confirmation: Long Red Flash
            statusLed.flash(255, 0, 0, 1000); 
            
            // Wipe Everything
            preferences.clear(); // Explicitly clear the 'ae-temp' namespace keys
            nvs_flash_erase();   // Wipe the underlying partition
            nvs_flash_init();
            
            Serial.println("NVS Erased. Restarting...");
            delay(500);
            ESP.restart();
        }
    });
    
    // Handle JSON Pairing Data {"gauge_mac":"...", "key":"..."}
    bleService.setPairingDataCallback([](const char* json) {
        Serial.printf("Processing Pairing JSON: %s\n", json);
        // Minimal JSON parsing to avoid heavy library usage if possible, or use ArduinoJson if available (user has it in other projects)
        // Let's assume ArduinoJson is available or use String search
        String j = String(json);
        
        // Extract MAC
        int macIdx = j.indexOf("gauge_mac");
        String gaugeMacStr = "";
        if (macIdx > 0) {
            int start = j.indexOf(":", macIdx) + 1;
            // Skip quotes
            while (j[start] == ' ' || j[start] == '"') start++;
            int end = j.indexOf('"', start);
            gaugeMacStr = j.substring(start, end);
        }
        
        // Extract Key
        int keyIdx = j.indexOf("key");
        String keyStr = "";
        if (keyIdx > 0) {
            int start = j.indexOf(":", keyIdx) + 1;
            while (j[start] == ' ' || j[start] == '"') start++;
            int end = j.indexOf('"', start);
            keyStr = j.substring(start, end);
        }
        
        Serial.printf("Extracted MAC: %s, Key: %s\n", gaugeMacStr.c_str(), keyStr.c_str());
        
        if (gaugeMacStr.length() > 0 && keyStr.length() == 32) {
             preferences.putString("p_mac", gaugeMacStr);
             preferences.putString("p_key", keyStr);
             
             // Update Global MAC for main loop
             sscanf(gaugeMacStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &g_pairedMac[0], &g_pairedMac[1], &g_pairedMac[2], 
               &g_pairedMac[3], &g_pairedMac[4], &g_pairedMac[5]);
             
             // Update runtime peer immediately
             espNowService.addSecurePeer(gaugeMacStr.c_str(), keyStr.c_str());
             
             bleService.updatePaired(true); // Notify App success
        } else {
            Serial.println("Invalid Pairing Data");
        }
    });

    // Init Drivers
    Wire.begin(I2C_SDA, I2C_SCL); // Init Wire before TMP102
    if (!tmp102.begin(I2C_SDA, I2C_SCL)) {
        Serial.println("TMP102 Init Failed!");
    } else {
        tmp102.wakeup(); // Ensure continuous conversion mode
    }
    
    statusLed.begin();
    statusLed.begin();
    // statusLed.flash(0, 128, 0, 200); // REMOVED: Silent Boot

    // Load Paired MAC if exists - MOVED down after begin()

    // Init Services
    espNowService.begin();
    bleService.begin(deviceName.c_str());
    // Ensure the characteristic holds only the suffix for editing
    bleService.updateName(nameSuffix.c_str());
    
    // Load Paired MAC if exists (MUST be after espNowService.begin)
    String savedMac = preferences.getString("p_mac", "");
    if (savedMac.length() > 0) {
        sscanf(savedMac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &g_pairedMac[0], &g_pairedMac[1], &g_pairedMac[2], 
               &g_pairedMac[3], &g_pairedMac[4], &g_pairedMac[5]);
        
        String savedKey = preferences.getString("p_key", "");
        if (savedKey.length() == 32) {
             espNowService.addSecurePeer(savedMac.c_str(), savedKey.c_str());
        }
    }
    
    // Ensure Paired Char has correct MAC (Redundant if set in begin, but safe)
    // bleService update handled in begin()

    // Read Sensors
    float temp = tmp102.readTemperature();
    Serial.printf("Temperature: %.2f C\n", temp);
    
    // Broadcast ESPNow
    TempSensorData data;
    data.id = 22;
    data.temperature = temp;

    data.batteryVoltage = 3.3; // Placeholder, need ADC read if circuit supports it
    data.batteryLevel = 100;   // Placeholder
    
    // Set initial update interval (Booting... likely going to sleep soon unless connected)
    // Default to configured sleep interval
    data.updateInterval = sleepInterval;

    // Copy name safely
    memset(data.name, 0, sizeof(data.name));
    strncpy(data.name, deviceName.c_str(), sizeof(data.name) - 1);
    
    // Send Data (Unicast if Paired, Broadcast if Not)
    bool isPairedLocal = bleService.isPaired(); // or check preferences
    // Fallback: Check global MAC if flag is inconsistent (as seen in Loop)
    if (!isPairedLocal && (g_pairedMac[0] != 0 || g_pairedMac[1] != 0)) {
         isPairedLocal = true;
    }

    if (isPairedLocal) {
         espNowService.sendToPeer(data, g_pairedMac);
         // statusLed.flash(0, 128, 0, 50); // REMOVED: Silent wake-up for sleep mode
    } else {
         espNowService.broadcast(data);
         statusLed.flash(64, 64, 64, 50); // White Flash (Broadcast/Discovery)
    }

    // Update BLE
    bleService.updateTemperature(temp);
    bleService.updateBatteryLevel(100);

    stateStartTime = millis();
}

// Include ESP WiFi for channel switching
#include <esp_wifi.h>

void loop() {
    // Check for sleep interval change from BLE
    static uint32_t lastSleepInterval = bleService.getSleepInterval();
    if (bleService.getSleepInterval() != lastSleepInterval) {
        lastSleepInterval = bleService.getSleepInterval();
        preferences.putUInt("sleep_ms", lastSleepInterval);
        Serial.println("Saved new sleep interval to NVS");
        statusLed.flash(0, 0, 128, 200); // Blue flash on save (Dimmed)
    }

    // BLE Maintenance
    if (bleService.isConnected()) {
        isStayingAwake = true;
        
        static unsigned long lastUpdate = 0;
        if (millis() - lastUpdate > 5000) {
            float temp = tmp102.readTemperature();
            bleService.updateTemperature(temp);
            
            // Broadcast ESP-NOW Data so Gauge can see it during pairing
            // CYCLE CHANNELS to ensure Gauge finds us regardless of its WiFi channel
            TempSensorData data;
            data.id = 22; 
            data.temperature = temp;
            data.batteryVoltage = 3.3; 
            data.batteryLevel = 100;
            data.updateInterval = 5000; // 5 seconds when connected/awake

            String suffix = preferences.getString("name", "");
            String nameForEspNow = "AE Temp Sensor";
            if (suffix.length() > 0) nameForEspNow = suffix;
            
            // Start with Generic Broadcast Address
            memset(data.name, 0, sizeof(data.name));
            strncpy(data.name, nameForEspNow.c_str(), sizeof(data.name) - 1);

            // If Paired, send unicast to Gauge Address (Secure Peer)
            // If Not Paired, broadcast to FF:FF... on all channels
            
            bool isPaired = bleService.isPaired();
            // Robustness: If NVS or Flag is wrong, check if we have the peer loaded (and Global Mac set)
            if (!isPaired && (g_pairedMac[0] != 0 || g_pairedMac[1] != 0)) {
                 if (esp_now_is_peer_exist(g_pairedMac)) {
                     Serial.println("DEBUG: isPaired flag was false, but Secure Peer exists. Forcing Paired Mode.");
                     isPaired = true;
                     bleService.setPaired(true); // Sync flag
                 }
            }
            
            if (isPaired) {
                 // UNICAST ENCRYPTED to GAUGE
                 // Note: We need to know the Gauge's MAC (it's in g_pairedMac/Preferences)
                 // But wait, the Gauge is usually the AP/Receiver.
                 // Actually ESP-NOW is peer-to-peer.
                 // We added the secure peer in setPairedCallback.
                 
                 // We need to pass the target MAC to broadcast()? Or update broadcast() to take an address.
                 // For now, let's just make broadcast() use the stored peer or broadcast depending on state.
                 // BUT wait, broadcast() in service hardcodes "broadcastAddress".
                 // We need to overload it or change it.
                 // Let's modify broadcast() to take an optional address.
                 
                 // Actually, if we are paired, we should ONLY talk to the Gauge?
                 // Or do we still broadcast for others?
                 // Gauge expects "ID 22" from Sensor. 
                 // If encrypted, Gauge decrypts.
                 
                 // We need to expose a method `sendToPeer(data, mac)` in EspNowService.
                 
                 espNowService.sendToPeer(data, g_pairedMac);
                 statusLed.flash(0, 128, 0, 50); // Green Flash on Unicast (Dimmed)
            } else {
                 // BROADCAST Cycling (Hunt for Gauge)
                 // Only needed during pairing/discovery
                 statusLed.flash(0, 0, 128, 50); // Blue Flash on Broadcast Burst (Dimmed)
                 for (int ch = 1; ch <= 13; ch++) {
                    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
                    delay(10); 
                    espNowService.broadcast(data);
                 }
                 esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
            }
            
            lastUpdate = millis();
        }
    } else {
        isStayingAwake = false;
    }

    // Deep Sleep Logic: Only if Paired
    if (bleService.isPaired()) {
        if (!isStayingAwake && (millis() - stateStartTime > AWAKE_TIME_MS)) {
            Serial.println("Going to sleep...");
            statusLed.off();
            tmp102.shutdown();
            
            uint64_t sleepUs = (uint64_t)bleService.getSleepInterval() * 1000ULL;
            esp_sleep_enable_timer_wakeup(sleepUs);
            esp_deep_sleep_start();
        }
    } else {
        // Not Paired: Stay Awake, Blue heartbeat to indicate "Ready to Pair"
        static unsigned long lastFlash = 0;
        if (millis() - lastFlash > 2000) {
             statusLed.flash(0, 0, 25, 100); // Faint Blue (Dimmed from 50)
             lastFlash = millis();
        }
    }
    
    delay(10);
}
