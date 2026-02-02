#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include "drivers/tmp102.h"
#include "drivers/neopixel.h"
#include "services/ble_service.h"
#include <services/espnow_service.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <WiFiClientSecure.h>

// Pins
#define I2C_SDA 8
#define I2C_SCL 9
#define NEOPIXEL_PWR 5
#define NEOPIXEL_PWR 5
#define NEOPIXEL_DATA 3
#define BOOT_PIN 9

#include <OTA-Hub.hpp>
#include <ota-github-cacerts.h>

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
bool g_isTimerWakeup = false;

volatile bool g_indirectOtaPending = false;
struct_message_ota_trigger g_otaTrigger;

void onDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    if (len == sizeof(struct_message_ota_trigger)) {
        struct_message_ota_trigger trigger;
        memcpy(&trigger, incomingData, sizeof(trigger));
        if (trigger.messageID == 110) {
            trigger.version[sizeof(trigger.version)-1] = '\0'; // Safety
            Serial.printf("[ESP-NOW] OTA Trigger: Ver='%s' (Current='%s'), Force=%d\n", 
                          trigger.version, OTA_VERSION, trigger.force);

            if (!trigger.force && strcmp(trigger.version, OTA_VERSION) == 0) {
                Serial.println("[ESP-NOW] OTA Ignored: Already on target version.");
                return;
            }

            Serial.println("[ESP-NOW] OTA Trigger Accepted!");
            memcpy((void*)&g_otaTrigger, &trigger, sizeof(g_otaTrigger));
            g_indirectOtaPending = true;
        }
    }
}

void setup() {
    Serial.begin(115200);
    // Wait a bit for serial if USB connected
    delay(1000); 
    Serial.println("AE Temp Sensor Starting...");
    
    gpio_hold_dis((gpio_num_t)NEOPIXEL_PWR);
    gpio_hold_dis((gpio_num_t)NEOPIXEL_DATA); // Release Data Pin Hold
    gpio_deep_sleep_hold_dis(); 
    gpio_deep_sleep_hold_dis(); // Ensure global hold is off if used
    
    // Init Boot Pin
    pinMode(BOOT_PIN, INPUT_PULLUP);

    // Check Wakeup Cause
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        g_isTimerWakeup = true;
        Serial.println("Wakeup: TIMER (Fast Sleep Mode)");
    } else {
        Serial.println("Wakeup: MANUAL/POWER (POR) -> Staying Awake 30s");
        isStayingAwake = true; // Ensure 30s awake window on boot/manual reset
    }

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
        Serial.printf("Processing Pairing JSON/CMD: %s\n", json);
        
        if (String(json) == "PAIRING") {
            Serial.println("Received PAIRING command via BLE. Forcing broadcast mode for 5 mins.");
            espNowService.setForceBroadcast(true);
            // We use a global or local static to track time
            return;
        }

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

    bleService.setWifiCallback([](const char* ssid, const char* pass) {
        Serial.printf("Received WiFi Creds via BLE: SSID='%s'\n", ssid);
        // Store in global trigger struct for upcoming OTA
        memset(&g_otaTrigger, 0, sizeof(g_otaTrigger)); // Clear first to avoid garbage
        g_otaTrigger.messageID = 110;
        strncpy(g_otaTrigger.ssid, ssid, sizeof(g_otaTrigger.ssid) - 1);
        strncpy(g_otaTrigger.pass, pass, sizeof(g_otaTrigger.pass) - 1);
        // Force flag is set in Force Callback
    });

    bleService.setForceOtaCallback([]() {
        Serial.println("FORCE OTA Triggered via Direct BLE!");
        // We assume WiFi creds were just sent.
        // If not, we might fail to connect.
        // Check if SSID is present?
        if (strlen(g_otaTrigger.ssid) == 0) {
             Serial.println("Aborting Force OTA: No WiFi SSID set.");
             return;
        }

        g_otaTrigger.messageID = 110;
        g_otaTrigger.force = true;
        // URL left empty -> triggers OTA::isUpdateAvailable() logic in loop()
        
        g_indirectOtaPending = true;
    });

    // Init Drivers
    Wire.begin(I2C_SDA, I2C_SCL); // Init Wire before TMP102
    if (!tmp102.begin(I2C_SDA, I2C_SCL)) {
        Serial.println("TMP102 Init Failed!");
    } else {
        tmp102.wakeup(); // Ensure continuous conversion mode
        delay(35);       // Allow time for first conversion (26ms typical)
    }
    
    statusLed.begin();
    statusLed.begin();
    // statusLed.flash(0, 128, 0, 200); // REMOVED: Silent Boot

    // Load Paired MAC if exists - MOVED down after begin()

    // Init Services
    espNowService.begin();
    espNowService.registerRecvCallback(onDataRecv);
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
    
    // Reset Send Status before sending
    espNowService.resetSendStatus();

    // Broadcast ESPNow
    TempSensorData data;
    data.id = 22;
    data.temperature = temp;
    data.batteryVoltage = 3.3; // Placeholder
    data.batteryLevel = 100;   // Placeholder
    data.updateInterval = bleService.getSleepInterval();
    data.hardwareVersion = HW_VERSION;
    strncpy(data.firmwareVersion, String(OTA_VERSION).c_str(), sizeof(data.firmwareVersion) - 1);
    memset(data.name, 0, sizeof(data.name));
    strncpy(data.name, deviceName.c_str(), sizeof(data.name) - 1);

    // Send Data (Unicast if Paired, Broadcast if Not)
    bool isPairedLocal = bleService.isPaired(); 
    // Fallback: Check global MAC if flag is inconsistent
    if (!isPairedLocal && (g_pairedMac[0] != 0 || g_pairedMac[1] != 0)) {
         isPairedLocal = true;
    }

    if (isPairedLocal) {
         espNowService.sendToPeer(data, g_pairedMac);
    } else {
         espNowService.broadcast(data);
         statusLed.flash(64, 64, 64, 50); // White Flash (Broadcast/Discovery)
    }

    // WAIT FOR SEND FINISH (Essential for Fast Sleep)
    unsigned long sendStart = millis();
    while (!espNowService.sendFinished && (millis() - sendStart < 100)) {
        delay(1);
    }
    
    if (espNowService.sendFinished) {
         if (espNowService.sendSuccess) {
             delay(5); // success, small buffer
         } else {
             Serial.println("ESP-NOW Send Failed!");
             statusLed.flash(255, 128, 0, 50); // Orange Flash (Send Fail)
         }
    } else {
         Serial.println("ESP-NOW Send Timeout!");
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
            data.hardwareVersion = HW_VERSION;
            strncpy(data.firmwareVersion, String(OTA_VERSION).c_str(), sizeof(data.firmwareVersion) - 1);

            String suffix = preferences.getString("name", "");
            String nameForEspNow = "AE Temp Sensor";
            if (suffix.length() > 0) nameForEspNow += " - " + suffix;
            
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
            
            if (isPaired || espNowService.isForceBroadcast()) {
                 // UNICAST ENCRYPTED to GAUGE (or Broadcast if forced)
                 if (espNowService.isForceBroadcast()) {
                      // Forced Broadcast during Pairing
                      Serial.println("PAIRING MODE: Sending Broadcast Burst...");
                      statusLed.flash(0, 0, 128, 50); 
                      for (int ch = 1; ch <= 13; ch++) {
                         esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
                         delay(10); 
                         espNowService.broadcast(data);
                      }
                      esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
                      
                      static unsigned long pairingStartTime = millis();
                      if (millis() - pairingStartTime > 300000) {
                          Serial.println("Pairing Mode Timeout (5 mins).");
                          espNowService.setForceBroadcast(false);
                      }
                 }
                 
                 if (isPaired) {
                      espNowService.sendToPeer(data, g_pairedMac);
                      statusLed.flash(0, 128, 0, 50); // Green Flash on Unicast (Dimmed)
                 }
            } else {
                 // BROADCAST Cycling (Hunt for Gauge)
                 // Only needed during initial discovery when NOT paired
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
        uint32_t awakeWindow = g_isTimerWakeup ? 200 : AWAKE_TIME_MS; 
        
        if (!isStayingAwake && (millis() - stateStartTime > awakeWindow)) {
            uint32_t sleepMs = bleService.getSleepInterval();
            
            // Only sleep if interval is > 0. If 0, we stay awake (Always On).
            if (sleepMs > 0) {
                Serial.printf("Going to sleep for %u ms...\n", sleepMs);
                statusLed.off();
                // Shutdown Sensor
                if (!tmp102.shutdown()) {
                    Serial.println("TMP102 Shutdown Failed!");
                    statusLed.flash(255, 0, 0, 50); // Red Flash
                }
                
                // --- PHANTOM POWER FIX ---
                // Drive Data Pin LOW and Hold it to prevent leakage into LED
                pinMode(NEOPIXEL_DATA, OUTPUT);
                digitalWrite(NEOPIXEL_DATA, LOW);
                gpio_hold_en((gpio_num_t)NEOPIXEL_DATA);

                // Hold NeoPixel Power LOW
                gpio_hold_en((gpio_num_t)NEOPIXEL_PWR);
                gpio_deep_sleep_hold_en(); // Enable Global Hold Logic
                
                // --- I2C BUS RECOVERY & LEAKAGE FIX ---
                Wire.end(); // Stop I2C Driver
                
                // Manual Bus Clear: Toggle SCL 9 times to unstick slave
                pinMode(I2C_SDA, INPUT_PULLUP);
                pinMode(I2C_SCL, OUTPUT);
                for (int i = 0; i < 9; i++) {
                    digitalWrite(I2C_SCL, HIGH);
                    delayMicroseconds(5);
                    digitalWrite(I2C_SCL, LOW);
                    delayMicroseconds(5);
                }
                // Stop Condition (SDA Low -> High while SCL High)
                pinMode(I2C_SDA, OUTPUT);
                digitalWrite(I2C_SDA, LOW);
                delayMicroseconds(5);
                digitalWrite(I2C_SCL, HIGH);
                delayMicroseconds(5);
                digitalWrite(I2C_SDA, HIGH);
                delayMicroseconds(5);

                // Final State: Input Pullup (High-Z + Pullup)
                pinMode(I2C_SDA, INPUT_PULLUP);
                pinMode(I2C_SCL, INPUT_PULLUP); 

                // --- GPIO WAKEUP FIX ---
                // Explicitly valid input config for Deep Sleep (AFTER Reset)
                gpio_config_t io_conf = {};
                io_conf.intr_type = GPIO_INTR_DISABLE;
                io_conf.mode = GPIO_MODE_INPUT;
                io_conf.pin_bit_mask = (1ULL << BOOT_PIN);
                io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
                io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
                gpio_config(&io_conf);

                // --- DEEP SLEEP START ---
                // Note: GPIO9 (Boot) is NOT an RTC pin on C3, so we cannot wake from it in Deep Sleep.
                // We only wake on Timer.
                
                uint64_t sleepUs = (uint64_t)sleepMs * 1000ULL;
                esp_sleep_enable_timer_wakeup(sleepUs);
                esp_deep_sleep_start();
            } else {
                 // Optional: Periodic debug to confirm we are awake
                 static unsigned long lastAwakeLog = 0;
                 if (millis() - lastAwakeLog > 10000) {
                     Serial.println("Always On Mode: Staying Awake...");
                     lastAwakeLog = millis();
                 }
            }
        }
    } else {
        // Not Paired: Stay Awake, Blue heartbeat to indicate "Ready to Pair"
        static unsigned long lastFlash = 0;
        if (millis() - lastFlash > 2000) {
             statusLed.flash(0, 0, 25, 100); // Faint Blue (Dimmed from 50)
             lastFlash = millis();
        }
    }

    // OTA Execution Logic
    if (g_indirectOtaPending) {
        g_indirectOtaPending = false;
        Serial.println("[OTA] Starting Update Process...");
        
        statusLed.flash(0, 0, 255, 500); // Blue Long Flash
        
        WiFi.disconnect(true);
        WiFi.mode(WIFI_STA);
        WiFi.begin(g_otaTrigger.ssid, g_otaTrigger.pass);
        
        int tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries < 30) {
            delay(500);
            tries++;
            Serial.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[OTA] WiFi Connected. Syncing Time...");
            configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            delay(2000); 
            
            WiFiClientSecure client;
            client.setCACert(OTAGH_CA_CERT);
            OTA::init(client);

            OTA::UpdateObject obj;
            String url = String(g_otaTrigger.url);

            if (g_otaTrigger.force && url.length() == 0) {
                 Serial.println("[OTA] Force Update with Empty URL. Checking Defaults...");
                 obj = OTA::isUpdateAvailable();
                 if (obj.condition != OTA::NO_UPDATE) {
                     Serial.println("[OTA] Default Update Found. Forcing Install.");
                     obj.condition = OTA::NEW_DIFFERENT;
                 } else {
                     Serial.println("[OTA] No Default Update Found to Force.");
                 }
            } else {
                obj.condition = OTA::NEW_DIFFERENT;
                obj.tag_name = String(g_otaTrigger.version);
                
                if (url.startsWith("http")) {
                    int protoEnd = url.indexOf("://");
                    int pathStart = url.indexOf("/", protoEnd + 3);
                    if (pathStart > 0) {
                        obj.redirect_server = url.substring(protoEnd + 3, pathStart);
                        obj.firmware_asset_endpoint = url.substring(pathStart);
                    } else {
                        obj.firmware_asset_endpoint = url;
                    }
                } else {
                    obj.firmware_asset_endpoint = url;
                }
            }

            if (OTA::performUpdate(&obj, true, true, nullptr) == OTA::SUCCESS) {
                Serial.println("[OTA] Success! Restarting...");
                delay(1000);
                ESP.restart();
            }
        }
        Serial.println("\n[OTA] Failed or Canceled.");
        ESP.restart();
    }
    
    delay(10);
}
