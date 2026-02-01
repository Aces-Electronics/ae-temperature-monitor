# AE Temperature Monitor Firmware
Firmware for the Aces Electronics Temperature Monitor, utilizing ESP-NOW for low-latency communication with the Smart Shunt gateway.

## Features
- **Remote Monitoring**: Measures temperature using a high-accuracy sensor.
- **Battery Powered**: Optimized for low-power operation.
- **Wireless Relay**: Transmits data to the Smart Shunt via ESP-NOW.
- **Auto-Discovery**: Automatically detected and registered by the Smart Shunt and Cloud Dashboard.

## Communication Protocol (ESP-NOW)
The Sensor broadcasts a `struct_message_temp_sensor` payload which is received by the Smart Shunt. The Shunt then relays this data to the Cloud Dashboard for real-time alerts and historical logging.

## Build & Flash
The project uses PlatformIO.
```bash
pio run -t upload
```
