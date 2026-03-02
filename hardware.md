# Hardware Design — Battery-Powered LED Controller

## Power Budget First

This drives all component choices:

| State | Current draw |
|-------|-------------|
| LEDs OFF, ESP32 sleeping | ~1–5 mA |
| ESP32 active (Zigbee joining) | ~80–120 mA @ 3.3V |
| 30× WS2812B at 10% brightness | ~180 mA @ 5V |
| 30× WS2812B at 50% brightness | ~900 mA @ 5V |
| 30× WS2812B at 100% white | ~1800 mA @ 5V |

**The LEDs dominate.** A 2000 mAh LiPo at 50% brightness gives ~2–3 hours of ON time. When off/sleeping it can run for weeks.

---

## Shopping List

### 1. ESP32-C6 Board
Search: **"ESP32-C6 SuperMini"** — AliExpress ~$3–4

Compact, USB-C, has onboard 3.3V LDO. Easy to solder wires to.
Alternatively: **"ESP32-C6 DevKitC-1"** on Amazon (~$12) if you want the official dev board.

### 2. LiPo Battery
Search: **"3.7V 2000mAh LiPo battery JST connector"** — AliExpress ~$4–6

Or use an **18650 Li-Ion cell** (2600 mAh, more robust):
Search: **"18650 battery holder single"** + any branded 18650 cell (Samsung 25R, LG MJ1).

### 3. LiPo Charger
Search: **"TP4056 USB-C charging module with protection"** — AliExpress ~$1, get a 5-pack

Important: get the version **with protection circuit** (has 8 pins, not 6). It adds over-discharge and short-circuit protection.

### 4. Boost Converter (3.7V → 5V for LEDs)
Search: **"MT3608 boost converter module 5V"** — AliExpress ~$1, get a 5-pack

Pre-set to 5V output, or has a trim pot — set it to exactly 5.0V before connecting LEDs.

### 5. WS2812B LED Strip
Search: **"WS2812B LED strip 30 LEDs/m IP30"** — Amazon or AliExpress ~$8–15

IP30 (no waterproofing) for indoor use. 1 metre = 30 LEDs matches our firmware exactly.

### 6. Extras
- **330Ω resistor** (for the data line) — any resistor pack
- **Slide switch** (for power on/off) — search "mini slide switch SPDT"
- **JST 2-pin connectors** (to make battery removable)
- Thin wire (24–26 AWG)

---

## Wiring Diagram

```
                    USB-C (charging only)
                         │
                         ▼
[LiPo 3.7V] ──[SW1]──┬──[TP4056]── (charges battery)
                      │
                      ├──────────────── ESP32-C6 BAT/5V pin
                      │                (onboard LDO → 3.3V)
                      │
                      └──[MT3608]──── WS2812B +5V
                         (→ 5V out)

ESP32-C6 GPIO 8 ──[330Ω]──────────── WS2812B DIN
ESP32-C6 GND ─────────────────────── WS2812B GND
MT3608 GND ───────────────────────── WS2812B GND  (shared ground!)
TP4056 B- ────────────────────────── common GND
```

---

## Assembly Notes

**Set the MT3608 before connecting anything:** power it from the battery alone, measure the output with a multimeter, turn the trim pot until it reads 5.0V. Then connect the LEDs.

**The TP4056 sits between USB and the battery** — it only charges, it doesn't regulate output. The ESP32's onboard LDO handles regulation to 3.3V.

**Shared ground is critical** — the ESP32, TP4056, MT3608, and LED strip all must share the same GND rail. Without it, the GPIO 8 signal won't be read correctly by the LEDs.

**The slide switch** goes on the positive line from the battery to everything. Lets you cut power without unplugging the battery.

---

## Estimated Total Cost

| Item | Cost |
|------|------|
| ESP32-C6 SuperMini | ~$4 |
| 2000 mAh LiPo | ~$5 |
| TP4056 (×5 pack) | ~$2 |
| MT3608 (×5 pack) | ~$2 |
| WS2812B 1m strip | ~$10 |
| Misc (resistor, switch, wire) | ~$3 |
| **Total** | **~$26** |

No custom PCB needed — everything connects with wires on a small perfboard or even just with soldered leads.
