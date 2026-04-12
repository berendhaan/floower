# Floower ESPHome

This directory contains an [ESPHome](https://esphome.io) configuration that
replaces the original Bluetooth-based Flooware firmware with a WiFi-connected
device that integrates natively with **Home Assistant**.

## What changes

| Original firmware | ESPHome version |
|---|---|
| Bluetooth LE remote | WiFi + Home Assistant API |
| Mobile app control | Home Assistant dashboard |
| EEPROM-stored settings | Substitution variables in YAML |
| Deep-sleep mode | Standard ESP32 WiFi mode |

All physical touch interactions (tap, long-press, hold) are preserved exactly
as in the original firmware.

## Entities created in Home Assistant

| Entity | Type | Description |
|---|---|---|
| `light.floower_light` | Light | 7-LED NeoPixel ring – colour, brightness, Rainbow & Candle effects |
| `cover.floower_petals` | Cover | Open / close the petals |
| `sensor.floower_battery_level` | Sensor | Battery percentage (0–100 %) |
| `sensor.floower_battery_voltage` | Sensor | Battery voltage in V |
| `binary_sensor.floower_usb_powered` | Binary sensor | USB charger connected (HW rev ≥ 6) |
| `binary_sensor.floower_leaf_touch` | Binary sensor | Capacitive leaf currently touched |

## Touch behaviour

| Gesture | Action |
|---|---|
| **Tap** | Cycles: STANDBY → light on → petals open → petals close → STANDBY |
| **Long press (> 2 s)** | Starts Rainbow animation |
| **Hold (> 5 s)** | Flashes blue (WiFi-connection feedback) |

## Getting started

### 1. Prerequisites

- [ESPHome](https://esphome.io/guides/installing_esphome.html) installed
  (`pip install esphome` or use the Home Assistant add-on)
- A `secrets.yaml` file in this directory (copy from `secrets.yaml.example`)

### 2. Create your secrets file

```bash
cp esphome/secrets.yaml.example esphome/secrets.yaml
# Edit secrets.yaml with your WiFi credentials and keys
```

### 3. Servo calibration

The servo open/close positions are factory-calibrated and stored in the
Floower's EEPROM.  To find the correct values for your unit:

1. Flash the **original** firmware once and open the serial monitor
   (115 200 baud).
2. Look for a log line like:
   ```
   HW: 800 -> 1300, R7, SN0042, f1
   ```
   The two numbers are `servoClosed` and `servoOpen` in microseconds.
3. Convert to duty-cycle percentages for a 50 Hz signal (20 ms period):
   ```
   level% = (pulse_µs / 20 000) × 100
   ```
   Example: closed = 800 µs → **4.0 %**, open = 1 300 µs → **6.5 %**
4. Set `servo_min_level` and `servo_max_level` in `floower.yaml`
   (the `substitutions:` block at the top).

Default values (4.0 % / 6.5 %) work for most Floowers manufactured after
revision 5.

### 4. Touch threshold

| Hardware revision | Recommended `touch_threshold` |
|---|---|
| Rev 7 (SN 0133+) | `45` (default) |
| Rev 8+ | `50` |

Lower values increase sensitivity.

### 5. Compile and flash

```bash
# First flash – connect via USB-C
esphome run esphome/floower.yaml

# Subsequent updates – over the air
esphome run esphome/floower.yaml
```

### 6. Add to Home Assistant

The device will appear automatically in **Settings → Devices & Services**
via the ESPHome integration once it is on the same network.

## Colour scheme

The tap gesture cycles through eight preset colours (matching the original
firmware defaults):

| # | Colour | Hue |
|---|---|---|
| 1 | White | — |
| 2 | Yellow | 0.16 |
| 3 | Orange | 0.06 |
| 4 | Red | 0.00 |
| 5 | Pink | 0.93 |
| 6 | Purple | 0.81 |
| 7 | Blue | 0.61 |
| 8 | Green | 0.30 |

You can change the colour at any time from Home Assistant using
`light.floower_light`.

## Differences from the original firmware

- **No BLE** – WiFi is always on; deep sleep is not used.
- **No EEPROM settings** – servo calibration, touch threshold, and the
  colour scheme are configured via substitution variables in `floower.yaml`.
- **No mobile app** – all control is via Home Assistant or physical touch.
- The hold-touch gesture no longer starts BLE advertising; it flashes blue
  as a WiFi-status indicator instead.
