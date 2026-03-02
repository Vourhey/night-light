# Night Light

ESP32-C6 Zigbee end device firmware that controls a WS2812B LED strip. Appears in Zigbee2MQTT as an **Extended Color Light** with on/off, brightness, and hue/saturation color control.

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32-C6 |
| LED strip | WS2812B, 30 LEDs |
| Data pin | GPIO 8 |
| Power | Battery (LED strip powered separately) |

Wire the strip's DIN through a 330Ω resistor to GPIO 8. Share GND between the ESP32 and the strip. Do not power the strip from the ESP32's pins.

## Build & Flash

Prerequisites: [ESP-IDF v5.3+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html) installed and sourced.

```bash
get_idf                          # activate ESP-IDF environment
idf.py build                     # compile
idf.py -p /dev/cu.usbserial-* flash monitor   # flash and open serial console
```

On first build, if you changed the device role or flash size settings, delete the cached config first:

```bash
rm sdkconfig && idf.py build
```

## Pairing with Zigbee2MQTT

1. **Enable Permit Join** in the Zigbee2MQTT web UI (or send `{"value": true}` to `zigbee2mqtt/bridge/request/permit_join` via MQTT).
2. **Power on** the ESP32-C6. On first boot (factory-new) it automatically starts scanning channels 11–26 for a network to join.
3. Watch the serial monitor — you should see:
   ```
   I (XXXX) zb_light: Factory-new device, starting network steering
   I (XXXX) zb_light: Joined network. PAN ID: 0xXXXX, Channel: XX
   ```
4. Zigbee2MQTT auto-discovers the device and exposes `state`, `brightness`, and `color` (hue/saturation) controls.

### Re-pairing / factory reset

The device stores its network credentials in the `zb_storage` flash partition. To force re-pairing, erase that partition or do a full flash erase:

```bash
idf.py -p /dev/cu.usbserial-* erase-flash
idf.py -p /dev/cu.usbserial-* flash
```

## What to expect in Zigbee2MQTT

After joining, the device appears as:

- **Device type**: End Device
- **Model**: Extended Color Light (0x010D)
- **Exposed controls**:
  - `state` — ON / OFF
  - `brightness` — 0–254
  - `color` — hue (0–360°) and saturation (0–100%)

State (on/off, brightness, color) is persisted to flash and restored automatically on reboot.
