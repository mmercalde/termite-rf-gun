# Plan: Heltec WiFi LoRa 32 V3 as the consolidated gun controller

**Status:** PLANNED (evaluating). Not yet built.
**Date:** 2026-06-16
**Board:** Heltec WiFi LoRa 32 V3 (ESP32-S3FN8 + Semtech SX1262 + 0.96" SSD1306 OLED)
**Owns:** Aideepen/Heltec V3 2-pack already on hand (the LoRa-link boards).

## Why

Today the gun side is an ESP32-S3 Supermini running the inverter controller,
with a *separate* Lora32 planned for the 915 MHz out-of-band link. The Heltec
V3 already integrates the SX1262 LoRa radio **and** a status OLED on one
ESP32-S3 board. Migrating the gun controller onto a Heltec collapses three
things into one:

- inverter + RTD + fan controller (current firmware)
- 915 MHz LoRa node (deadman link / radio-dark-on-2.4 GHz plan)
- gun-side status display (temps, RF state, link health, faults)

The 2.45 GHz desense problem stays solved: LoRa is 915 MHz, well clear of the
magnetron, and WiFi/BLE (2.4 GHz) stay OFF per the radio-dark plan.

## Hard constraint: Heltec reserves many GPIOs

These are used by onboard peripherals and are **off-limits** for our signals:

| Function | Pins |
|----------|------|
| SX1262 LoRa | 8 (NSS), 9 (SCK), 10 (MOSI), 11 (MISO), 12 (RST), 13 (BUSY), 14 (DIO1) |
| OLED I2C (SSD1306) | 17 (SDA), 18 (SCL), 21 (RST) |
| Vext (powers OLED/ext, active LOW) | 36 |
| Battery / misc | 1 (VBAT ADC), 37 (ADC ctrl), 35 (LED), 0 (PRG button) |
| USB / strapping (avoid) | 19, 20, 45, 46 |
| SPI flash (internal) | 26-32 |

**Six of the current Supermini pins collide** and must move:
RTD SCK 11 / MOSI 12 / MISO 13 (= LoRa SPI bus), CS_mag 1 (= VBAT), fan_mag_tach
10 (= LoRa MOSI), fan_hs_pwm 8 (= LoRa NSS).

## Proposed remap (12 signals onto free pins)

Free, safe pins on the V3: 2, 3, 4, 5, 6, 7, 33, 34, 38, 39, 40, 41, 42, 47, 48.

| Signal | Supermini (now) | Heltec V3 (proposed) | note |
|--------|-----------------|----------------------|------|
| inverter cmd      | 4  | 4  | keep |
| inverter status   | 5  | 5  | keep (1k + ~1k pulldown divider) |
| zero-cross        | 7  | 7  | keep |
| fan_mag PWM       | 2  | 2  | keep |
| fan_hs tach       | 3  | 3  | keep |
| RTD CS_hs         | 6  | 6  | keep |
| RTD SCK           | 11 | 33 | move (was LoRa MISO) |
| RTD MOSI/SDI      | 12 | 34 | move (was LoRa RST) |
| RTD MISO/SDO      | 13 | 47 | move (was LoRa BUSY) |
| RTD CS_mag        | 1  | 48 | move (was VBAT) |
| fan_mag tach      | 10 | 38 | move (was LoRa MOSI) |
| fan_hs PWM        | 8  | 39 | move (was LoRa NSS) |

Spare: 40, 41, 42. ESP32-S3 has no input-only pins and all GPIO are
interrupt-capable, so tach inputs / LEDC PWM / bit-bang SPI all work on any of
the above. The manual-CS RTD driver already takes explicit pins, so only the
`PIN_RTD_*` / `PIN_FAN_*` constants change — no logic changes.

**VERIFY before wiring:** confirm these internal-pin assignments against the
exact board's pinout (Heltec revs occasionally shuffle). RF discipline near the
magnetron still applies (RC filters on sense lines are load-bearing).

## OLED status page (new)

0.96" 128x64 mono SSD1306 on I2C (SDA 17 / SCL 18 / RST 21), powered via Vext
(drive GPIO36 LOW to enable). Library: U8g2 or Heltec_ESP32. Proposed readout:
mag °C / hs °C, RF ON/OFF + duty, LoRa link OK/LOST (deadman heartbeat age),
fault flags. Keep refresh modest; OLED is on the I2C bus only, no pin conflict
with the remap above.

## Migration steps (when committed)

1. Remap `PIN_*` constants in `panasonic_dual_igbt_esp32s3` per the table.
2. Add SX1262 LoRa deadman link (gun fires only while receiving heartbeat;
   cut output within ~200-300 ms of link loss; fail-safe to OFF).
3. Add OLED status page.
4. Bench-verify inverter + dual RTD + fans on the Heltec before going to the gun.

---

# Open verification items (RTD/fan subsystem) — carry-over

These are NOT done yet on the current Supermini build and carry into the Heltec
plan unchanged:

- [ ] **2nd RTD (heatsink) board** — wire a known-good MAX31865 to CS_hs
      (Supermini GPIO6). Magnetron channel already reads ~24 C (raw ~8349,
      flt 0x00). Expect heatsink to read ambient once connected; currently shows
      -242 C = "no sensor present", which is the correct unconnected reading.
- [ ] **Fan speed control** — verify the 25 kHz LEDC PWM actually varies both
      4-wire fan speeds across the thermal curve (not just full-on).
- [ ] **Fan tach reporting** — confirm both tach inputs return real RPM in the
      web UI / `status` (currently "0 rpm @ 40%" with fans possibly not spun up
      or tach not wired).
- [ ] **Commit/push firmware v19** — adds live temps (C and F, both sensors) to
      the serial `status` command. Flashed/tested but may not be pushed yet.

## Known-good hardware inventory (post 2026-06-16 bench)

- MAX31865 boards: 1 new (reads 24.4 C on gun, confirmed) + old #2 (passed comms
  test). Old #1 = dead (discarded). 1 bricked ESP32-S3 (discarded).
- Root cause of the long RTD saga: MAX31865 CS->clock polarity-lock gap missing
  in the ESP32 SPI driver; fixed by manual-CS in firmware v18. See
  `docs/failure_analysis/2026-06-15_max31865_rtd_bringup_blocked.md`.
