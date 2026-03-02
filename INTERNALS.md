# Night Light — Technical Internals

Target audience: firmware engineers working on or maintaining this codebase.

---

## Table of Contents

1. [File Map](#file-map)
2. [Boot Sequence](#boot-sequence)
3. [Task Architecture](#task-architecture)
4. [LED Subsystem](#led-subsystem)
5. [Zigbee Subsystem](#zigbee-subsystem)
6. [State Flow: Zigbee Command → LED Output](#state-flow-zigbee-command--led-output)
7. [Startup State Restore (Reboot)](#startup-state-restore-reboot)
8. [Flash Layout](#flash-layout)
9. [Configuration Reference](#configuration-reference)
10. [Key Constants](#key-constants)

---

## File Map

```
main/
├── main.c               Entry point. Boots NVS, inits LED, spawns Zigbee task.
├── led_strip_control.h  Public LED API + night_light_state_t type definition.
├── led_strip_control.c  RMT driver init, HSV→RGB conversion, LED FreeRTOS task.
├── zigbee_light.h       Zigbee constants (endpoint, device ID, defaults).
└── zigbee_light.c       Zigbee stack init, cluster setup, callbacks, signal handler.
```

---

## Boot Sequence

```
app_main()                          [main.c]
│
├── nvs_flash_init()
│     NVS partition is required by the ZBOSS stack for internal key storage.
│     If the partition is corrupted or version-mismatched, it is erased and
│     re-initialised.
│
├── led_strip_control_init()        [led_strip_control.c]
│     Creates mutex + event group.
│     Configures RMT peripheral for GPIO 8 at 10 MHz.
│     Registers led_strip handle (WS2812, GRB format, 30 LEDs).
│     Clears the strip (all pixels off).
│     Spawns led_task (priority 5, any core).
│
└── xTaskCreatePinnedToCore(zigbee_task, core 0)
      ZBOSS requires its task to run on a fixed core; core 0 is used.
      │
      └── zigbee_task()             [zigbee_light.c]
            │
            ├── zb_stack_init()
            │     esp_zb_platform_config()   — native 802.15.4 radio
            │     esp_zb_init()              — role: End Device, keep-alive 4 s
            │     esp_zb_sleep_enable(true)  — enable sleepy end-device mode
            │     Build cluster list         — color dimmable light profile
            │     Register endpoint 1        — HA profile, device 0x010D
            │     Register action callback   — zb_action_handler
            │     Set channel mask           — all channels 11–26
            │
            ├── esp_zb_start(false)
            │     Starts the ZBOSS stack. 'false' = do not auto-start BDB.
            │     Stack fires ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP when ready.
            │
            └── esp_zb_stack_main_loop_iteration()
                  Runs the ZBOSS cooperative scheduler forever.
                  All Zigbee callbacks fire from within this loop.
```

---

## Task Architecture

Two FreeRTOS tasks run concurrently after boot:

```
┌─────────────────────────────────┐     ┌─────────────────────────────────┐
│       zigbee_task (core 0)      │     │        led_task (any core)      │
│                                 │     │                                 │
│  ZBOSS main loop                │     │  xEventGroupWaitBits(           │
│  ↓                              │     │    LED_UPDATE_BIT,              │
│  Attribute write callback       │     │    portMAX_DELAY)  ← blocks     │
│  ↓                              │     │                                 │
│  updates s_nl_state             │  ┌──│  wakes up                       │
│  (under mutex)                  │  │  │  ↓                              │
│  ↓                              │  │  │  copy s_state (under mutex)     │
│  sets LED_UPDATE_BIT ───────────┼──┘  │  ↓                              │
│                                 │     │  HSV → RGB                      │
│                                 │     │  ↓                              │
│                                 │     │  led_strip_set_pixel × N        │
│                                 │     │  led_strip_refresh()            │
│                                 │     │  ↓                              │
│                                 │     │  blocks again                   │
└─────────────────────────────────┘     └─────────────────────────────────┘
```

**Shared resources:**

| Resource | Type | Purpose |
|----------|------|---------|
| `s_state` | `night_light_state_t` | Current on/off + color state |
| `s_mutex` | `SemaphoreHandle_t` | Guards `s_state` against concurrent access |
| `s_events` | `EventGroupHandle_t` | `LED_UPDATE_BIT` (bit 0) signals the LED task |

The LED task is purely reactive — it does zero work until signalled. This means the CPU is free to sleep between Zigbee polls.

---

## LED Subsystem

### `led_strip_control_init()` — `led_strip_control.c:85`

Called once from `app_main`. Sets up:
- FreeRTOS mutex and event group
- RMT peripheral via `led_strip_new_rmt_device()` (10 MHz clock, 64 symbols/block, no DMA)
- Clears strip to all-off
- Spawns `led_task`

### `led_strip_control_update()` — `led_strip_control.c:123`

The only public write path for LED state. Safe to call from any task.

1. Takes `s_mutex`
2. Copies the new `night_light_state_t` into `s_state`
3. Releases mutex
4. Sets `LED_UPDATE_BIT` to wake the LED task

### `led_task()` — `led_strip_control.c:54`

Infinite loop:
1. Blocks on `LED_UPDATE_BIT` (clears bit on wake)
2. Copies `s_state` under mutex
3. If `state.on == false`: writes RGB(0,0,0) to all pixels
4. If `state.on == true`: converts HSV→RGB and writes to all pixels
5. Calls `led_strip_refresh()` to clock data out via RMT

### `hsv_to_rgb()` — `led_strip_control.c:24`

Pure integer implementation, no floating point.

- Input: `h8` (0–254 → 0–360°), `s8` (0–254), `v8` (0–254)
- Shortcut: if saturation == 0, returns greyscale `(v, v, v)`
- Algorithm: standard 6-sector HSV with integer p/q/t intermediates
- The `level` attribute scales `v8` before calling this function:
  `v = level * 255 / 254`

---

## Zigbee Subsystem

### `zb_stack_init()` — `zigbee_light.c:195`

Configures and registers everything before the stack starts:

1. **Platform config** — uses the native IEEE 802.15.4 radio (no UART/SPI host)
2. **Stack config** — End Device role, 4 s keep-alive poll interval, 64-minute aging timeout
3. **Sleep** — `esp_zb_sleep_enable(true)` allows ZBOSS to enter light sleep between polls
4. **Cluster list** — built with `esp_zb_color_dimmable_light_clusters_create()`:
   - Basic cluster (ZCL version, power source = battery)
   - Identify cluster
   - Groups cluster
   - Scenes cluster
   - On/Off cluster (default: off)
   - Level Control cluster (default: 254)
   - Color Control cluster (HS mode, default X/Y values)
5. **Endpoint** — endpoint 1, HA profile (0x0104), device type Extended Color Light (0x010D)
6. **Callbacks** — `zb_action_handler` registered for all ZCL attribute writes
7. **Channel mask** — all channels 11–26 (coordinator selects the best one)

### `esp_zb_app_signal_handler()` — `zigbee_light.c:143`

Called by ZBOSS whenever a network event occurs. This function is **not static** — it is a weak symbol defined in the SDK that the application overrides.

| Signal | Meaning | Action taken |
|--------|---------|-------------|
| `SKIP_STARTUP` | Stack ready | Start BDB initialization |
| `DEVICE_FIRST_START` | Factory-new, no stored network | Start network steering (scan + join) |
| `DEVICE_REBOOT` | Known network found in flash | Restore ZCL attributes → drive LEDs |
| `STEERING` success | Joined a network | Log PAN ID and channel |
| `STEERING` failure | No network found | Schedule retry via `steering_retry()` after 1 s |
| `LEAVE` | Removed from network | Restart steering immediately |

### `steering_retry()` — `zigbee_light.c:135`

Alarm callback scheduled by `esp_zb_scheduler_alarm`. Retries
`esp_zb_bdb_start_top_level_commissioning(NETWORK_STEERING)` after a 1-second
delay. Repeats indefinitely until the device joins.

### `zb_action_handler()` — `zigbee_light.c:122`

Top-level ZCL action dispatcher. Currently handles one callback type:

- `ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID` → `zb_attribute_handler()`

All other callback IDs are logged at WARN level and ignored.

### `zb_attribute_handler()` — `zigbee_light.c:83`

Processes incoming ZCL attribute writes from the coordinator/controller.
Updates `s_nl_state` and calls `apply_state()` for each recognised attribute:

| Cluster | Attribute | Field updated |
|---------|-----------|--------------|
| On/Off (0x0006) | OnOff (0x0000) | `s_nl_state.on` |
| Level Control (0x0008) | CurrentLevel (0x0000) | `s_nl_state.level` |
| Color Control (0x0300) | CurrentHue (0x0000) | `s_nl_state.hue` |
| Color Control (0x0300) | CurrentSaturation (0x0001) | `s_nl_state.saturation` |

ZBOSS automatically persists all ZCL attribute values to the `zb_storage`
FAT partition after each write — no explicit save call is needed.

### `apply_state()` — `zigbee_light.c:28`

Thin wrapper: calls `led_strip_control_update(&s_nl_state)`.
Exists to keep the Zigbee side decoupled from LED implementation details.

### `apply_state_from_zcl()` — `zigbee_light.c:35`

Called on reboot (when `DEVICE_REBOOT` signal is received). ZBOSS has already
loaded persisted attributes from flash. This function reads them back via
`esp_zb_zcl_get_attribute()` and populates `s_nl_state`, then calls
`apply_state()` so the LEDs immediately reflect the pre-reboot state.

---

## State Flow: Zigbee Command → LED Output

```
Zigbee2MQTT / Home Assistant
        │
        │  ZCL Write Attribute command
        ▼
ZBOSS stack (inside esp_zb_stack_main_loop_iteration)
        │
        │  fires ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID
        ▼
zb_action_handler()
        │
        ▼
zb_attribute_handler()
        │  updates s_nl_state.on / .level / .hue / .saturation
        ▼
apply_state()
        │
        ▼
led_strip_control_update()          [crosses task boundary]
        │  mutex lock → copy state → mutex unlock
        │  sets LED_UPDATE_BIT
        ▼
led_task() wakes
        │
        ├─ if off: set all pixels to (0,0,0)
        │
        └─ if on:  v = level * 255 / 254
                   rgb = hsv_to_rgb(hue, saturation, v)
                   set all pixels to rgb
        │
        ▼
led_strip_refresh()  →  RMT peripheral clocks data to WS2812B strip
```

---

## Startup State Restore (Reboot)

```
Power on
    │
    ▼
ZBOSS reads zb_storage FAT partition
    │  All ZCL attribute values from last session are loaded into RAM
    ▼
esp_zb_app_signal_handler receives DEVICE_REBOOT
    │
    ▼
apply_state_from_zcl()
    │  esp_zb_zcl_get_attribute() × 4  (on/off, level, hue, sat)
    │  populates s_nl_state
    ▼
apply_state()  →  led_strip_control_update()  →  led_task wakes  →  LEDs restored
```

If `zb_storage` is empty (first boot or after erase), ZBOSS fires
`DEVICE_FIRST_START` instead and the device skips state restore,
going straight to network steering.

---

## Flash Layout

```
Address    Size    Name         Purpose
─────────────────────────────────────────────────────────────
0x9000     20 KB   nvs          ESP-IDF NVS (Zigbee internal keys)
0xe000     8 KB    otadata      OTA boot slot selector
0x10000    1.25MB  app0         Active firmware (OTA slot 0)
0x150000   1.25MB  app1         OTA update target (slot 1)
0x290000   64 KB   zb_storage   ZBOSS attribute persistence (FAT)
0x2A0000   4 KB    zb_fct       Zigbee factory config (IEEE address)
```

Total used: ~2.63 MB. Requires 4 MB flash minimum.

---

## Configuration Reference

**`sdkconfig.defaults`** — values applied on first `idf.py build` (or after `rm sdkconfig`):

| Key | Value | Effect |
|-----|-------|--------|
| `CONFIG_ZB_ENABLED` | y | Enables Zigbee stack |
| `CONFIG_ZB_ZED` | y | Selects End Device library (`libesp_zb_api.ed.a`) |
| `CONFIG_ESPTOOLPY_FLASHSIZE_4MB` | y | Matches physical flash size |
| `CONFIG_PARTITION_TABLE_CUSTOM` | y | Uses `partitions.csv` |
| `CONFIG_IEEE802154_ENABLED` | y | Enables 802.15.4 radio driver |
| `CONFIG_MBEDTLS_CMAC_C` | y | Required for Zigbee AES-128-CMAC |
| `CONFIG_MBEDTLS_KEY_EXCHANGE_ECJPAKE` | y | Required for Zigbee install code |
| `CONFIG_MBEDTLS_HARDWARE_*` | n | Disabled — conflicts with Zigbee on C6 |
| `CONFIG_PM_ENABLE` | y | Enables ESP power management |
| `CONFIG_FREERTOS_USE_TICKLESS_IDLE` | y | Allows light sleep between Zigbee polls |

---

## Key Constants

| Constant | File | Value | Meaning |
|----------|------|-------|---------|
| `LED_STRIP_GPIO` | `led_strip_control.h:6` | 8 | Data pin for WS2812B |
| `LED_STRIP_LED_COUNT` | `led_strip_control.h:7` | 30 | Number of LEDs in the strip |
| `HA_ENDPOINT` | `zigbee_light.h:6` | 1 | Zigbee endpoint number |
| `EXTENDED_COLOR_LIGHT_ID` | `zigbee_light.h:8` | 0x010D | Zigbee HA device type |
| `DEFAULT_LEVEL` | `zigbee_light.h:12` | 254 | Brightness on first boot |
| `keep_alive` | `zigbee_light.c:214` | 4000 ms | How often device polls parent when sleeping |
| `ed_timeout` | `zigbee_light.c:213` | 64 min | How long parent keeps device registered |
| `LED_UPDATE_BIT` | `led_strip_control.c:13` | BIT0 | Event group bit triggering LED refresh |

To change GPIO or LED count, edit the `#define` lines in `led_strip_control.h`
and rebuild. No other files need modification.
