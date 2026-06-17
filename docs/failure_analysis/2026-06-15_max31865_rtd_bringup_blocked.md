# MAX31865 PT100 RTD Bring-Up — Blocked by Bad Hardware

**Date:** 2026-06-15 (extended evening bench session)
**Board:** ESP32-S3 Supermini (HW-747) gun controller + 2x GY-MAX31865 (purple
CJMCU clone, PT100, RREF 430)
**Outcome:** Thermal-sensing subsystem **not brought up.** Every external
variable eliminated; failure isolated to defective MAX31865 boards and,
separately, a bricked ESP32-S3. Firmware is complete and verified correct;
it is gated only on working hardware.

---

## Goal

Replace the single NTC-on-ADC heatsink thermistor with **two PT100 RTDs via
MAX31865 (SPI)** — one on the magnetron fins, one on the IGBT heatsink — and
add variable-speed control of two 4-wire fans, each slaved to its own RTD
zone, with independent per-sensor thermal cutoffs.

## Firmware delivered (all committed, builds clean)

Lives in `panasonic_dual_igbt_esp32s3/` (the real flashed controller; the
identical `panasonic_strikedip_test` copy was reverted to baseline).

| Tag | Change |
|-----|--------|
| v11 | NTC ADC path → 2x `Adafruit_MAX31865` (software SPI, shared bus, per-board CS) |
| v12 | Variable-speed dual 4-wire fan control (25 kHz LEDC), per-fan tach, per-sensor thresholds |
| v13 | IGBT heatsink cutoff 100→80 °C / warn 70 °C (bench-safe per j-to-sink gradient) |
| v14 | Supermini no-parts pin map: dropped buttons (GPIO1/2) + log-tap (GPIO6) to free pins; GPIO9/BOOT unused |
| v15 | Swapped mag-fan tach ↔ mag RTD CS to honor already-wired fan (tach→GPIO10, CS→GPIO1) |
| v16 | Raw RTD + raw fault-byte boot diagnostic |
| v17 | Runtime-switchable wire mode + boot 2/3/4-wire sweep (`wire <2\|3\|4>`, NVS-persisted) |

### Final Supermini pin map (GPIO1–13 only; rest are bottom-pad/unreachable)
```
INVERTER (proven)   cmd GPIO4   status GPIO5   zero-cross GPIO7
RTD shared SPI bus  SCK GPIO11  MOSI/SDI GPIO12  MISO/SDO GPIO13
RTD chip-selects    mag CS GPIO1   hs CS GPIO6
Fans (4-wire)       mag PWM GPIO2 / tach GPIO10   hs PWM GPIO8(LED) / tach GPIO3
                    GPIO9 (BOOT) intentionally unconnected
```
Note: VIN must be fed **3.3 V on the VIN pin** (not the board's 3V3 output
pin — that back-feeds the regulator). RREF 430 = PT100 (resistor marked
4300/431); NOT the PT1000 value.

---

## Symptom

Both channels read garbage and never a valid temperature:
- raw value flips between `0` and `32767` (both rails) every read
- fault byte flips between `0x00` and `0xFF`
- decoded temp shows the two sentinels: **−242.02 °C / −403.6 °F** (resistance
  ≈ 0, "shorted") and **988.79 °C / 1811.8 °F** (resistance = full scale, "open")
- behavior identical across both boards and (later) two different ESP32s
- `flt = 0xFF` recurs — and a working MAX31865 **cannot** return 0xFF (the
  fault register's low two bits are always 0). 0xFF = MISO floating high = the
  chip is not coherently driving the bus.

## Diagnostic ladder — everything eliminated

| Suspect | Test | Result |
|---------|------|--------|
| Probe / element | Meter at leads: red-red 0.3 Ω, red-insulated 109.4 Ω | **Good** (matched-pair 3-wire PT100) |
| RREF / PT100 vs PT1000 | Resistor reads 4300/431 = 430 Ω | **Correct for PT100** |
| 3-wire board jumpers | Set per spec (2/3 closed, 24·3 center→3, cut left); also tried 2-wire | No mode reads correctly |
| Wire mode (firmware) | Boot sweep 2/3/4-wire | **All three byte-identical** → mode is not the variable |
| Power | Bench supply 3.3 V into VIN, common GND | Same garbage |
| Power (pin) | Found 3.3 V was on the board's 3V3 pin; moved to VIN | Same garbage |
| Ground | GND-to-GND ~0 Ω | Good |
| Wiring | Continuity on all 6; SDI/SDO swap experiment | Same |
| Library / SPI mode | Verified `Adafruit_MAX31865` constructor order against source; tried **hardware SPI** (correct mode) | Same garbage |
| Dual-instance code | Bare single-board Adafruit example, one instance | Same garbage |
| Different MCU | Fresh ESP32-S3 + bare example | Same garbage |

## Root-cause candidate (documented, untested due to dead hardware)

A reference video (ESP32 + MAX31865, ESP-IDF) documents that the MAX31865
**auto-detects SPI clock polarity when CS first goes low, and requires a GAP
between CS-low and the first clock edge.** The ESP32 SPI driver drops CS and
clock together (no gap), so the chip never locks SPI mode → exactly the
rail-flipping / 0xFF garbage seen here. His fix: drive CS as a **manual GPIO**
with an explicit gap, not the peripheral's hardware CS.

A bare-register **manual-CS test sketch** with a polarity-lock gap and a
write/read-back comms proof was written (`max31865_manualcs_test`,
tag `RTD-MANCS-v1`) to settle this. **It was never run** — see below.

## Why it wasn't settled

The bench accumulated dead hardware:
- **Both GY-MAX31865 boards**: fail identically; multiple Amazon reviews for
  this exact clone describe a bad-batch defect (uncuttable internal short on
  the 2/4 wire-select pads). Strong candidate for genuinely defective boards.
- **One ESP32-S3**: flash / USB-Serial-JTAG path intermittently failing.
  esptool reads chip ID, then "chip stopped responding" at flash-ID; ROM-mode
  `erase-region` drops mid-stream with "serial data stream stopped." Flash is
  corrupt and cannot be erased to recover. **Bricked.**

(Tooling note: the `esptool` in `$PATH` is old and lacks esp32s3; the IDE's
bundled one is at
`~/.arduino15/packages/esp32/tools/esptool_py/5.1.0/esptool`. Also a stale
process holding `/dev/ttyACM0` caused several false "stopped responding"
failures — close Serial Monitor / `fuser` the port before CLI flashing.)

## The one test that resolves it

Flash `max31865_manualcs_test` (`RTD-MANCS-v1`) on a **known-good ESP32** with
**one** MAX31865 wired. Read the first serial line:
```
comms test: wrote 0x10 -> read 0x10 ; wrote 0x00 -> read 0x00  => SPI OK (chip responds!)
```
- **Round-trips** → chip is alive; it was CS timing all along → convert main
  firmware to manual-CS and ship.
- **Does not round-trip** → MAX31865 boards are genuinely defective → replace
  with genuine Adafruit MAX31865 PT100 (3328): drop-in, same library, same
  pins, no firmware change.

## Cheaper / faster alternative

The **NTC thermistor path already calibrated in v10** (B=3868, R0=100972 Ω,
Rfixed=99 kΩ, Vsup=3311 mV) is sufficient for a heatsink/magnetron over-temp
cutoff and fan ramp — no SPI, pennies per sensor, sidesteps this entirely.
Can be folded back into the v17 firmware on request.

## Key learnings

1. **`0xFF` fault byte = no real comms** (MISO floating), not a sensor fault.
   A real MAX31865 never returns 0xFF.
2. **−242 °C / −403 °F = resistance 0 (shorted-low); 988 °C / 1811 °F =
   full-scale (open).** Both are sentinels, not temperatures. Flipping between
   them = floating/garbage bus, not a "reading that's off."
3. **2-wire and 4-wire are the SAME chip config** (`MAX31865_2WIRE` ==
   `MAX31865_4WIRE` == 0); only 3-wire differs. (A sweep that prints "2-wire"
   twice is expected, not a crash — this cost time chasing a phantom brownout.)
4. **MAX31865 needs a CS-low → first-clock GAP** for polarity auto-detect; the
   ESP32 SPI driver doesn't provide it. Manual-CS with an explicit gap is the
   documented fix.
5. **`Adafruit_MAX31865` is brand-agnostic** — it's a generic chip driver; the
   purple clone and the genuine board carry the identical IC. Only RREF,
   jumpers, and pins are board-specific.
6. **The CJMCU purple clone has a known bad-batch defect** (uncuttable 2/4
   wire-select short). Genuine Adafruit boards cut/configure cleanly.
7. **Project is not blocked** — the gun controller (inverter, status loop,
   fans, LoRa) runs without the RTDs; temps simply read faulted and fans
   default to full.

## Status

Firmware: **complete and correct at v17.** Hardware: **blocked on defective
parts.** Resolution is a 2-minute test (`RTD-MANCS-v1` on a good board) or a
pivot to NTC. No design or firmware rework is implied.
