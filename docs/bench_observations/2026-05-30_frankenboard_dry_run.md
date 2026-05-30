# Frankenboard dry-run observation — 2026-05-30

Mains applied to Frankenboard with **IGBTs not installed** (sitting on heatsink,
not soldered to PCB), ESP32 sending command via `force` + `on` (open-loop,
no status check). Scoped Q701 gate-to-emitter and CN701 pin 1 (status output).

## Setup

- Inverter board: F6645M301GP Frankenboard (Q701/Q702 IGBTs unsoldered)
- Q701 = original Toshiba GT50J327 (reinstalled after IGBT failure of replacement)
- Q702 = Infineon IHW30N120R5 (survived the original failure event)
- Both IGBTs mechanically on heatsink, electrically isolated from board
- ESP32 firmware: VK3HZ-correct rewrite (commit `3128125`)
- Mode: `force` + `on` (open-loop drive, ignoring status feedback)
- Drive: 222 Hz, ~40% duty on CN701 command input
- Mains: 240 V AC applied through 2 A fast-blow fuse
- Both scopes battery-powered (DS213 + UT81B) — fully isolated

## Observations

### CN701 pin 1 (status output) — UT81B + ESP32 statusinfo

**No signal observed.** ESP32 reports 0 edges on GPIO5. This is **expected
and correct behavior**:

- Inverter's status output is asserted only when CT701 senses primary current
- With IGBTs not installed, no primary current can flow regardless of gate drive
- ASIC correctly refrains from asserting status — no false signal
- Confirms the status-detection path on the ASIC is working as designed

### Q701 gate pad — DS213, gate-to-emitter probe

**Active gate drive present.** Voltage and frequency measured across a
7-second capture:

| Sample | Vp-p (V) | Frequency (kHz) | Notes |
|--------|----------|------------------|-------|
| ~0.5s  | 26.8     | 29.3             | Mid-range, safe ZVS region |
| ~2.0s  | 28.0     | 32.8             | Higher freq, safe ZVS region |
| ~3.5s  | 27.2     | 32.6             | Mid-high freq, safe ZVS region |
| ~5.0s  | 26.8     | **21.2**         | **BELOW f_r — capacitive mode** |
| ~6.5s  | 27.2     | **22.4**         | **BELOW f_r — capacitive mode** |

Voltage: stable ~27 Vp-p across all samples (bias rail solid at ~+20 V).

Frequency: **HUNTING between 21.2 kHz and 32.8 kHz**.

## Interpretation

### Why the controller hunts

The Panasonic ASIC closes a current-control loop using CT701 (input current
transformer) feedback. With no IGBTs and therefore no primary current, the
loop never converges:

- ASIC commands switching → no current measured → adjusts frequency to
  "increase power" → sweeps toward LC resonance to maximize transfer →
  still no current → keeps adjusting → never settles

The sweep range observed (21-33 kHz) represents the controller's full
operating range probing for a working operating point.

### Why this matches the failed bring-up

Per the SPICE simulation in `spice/results/` with the Frankenboard's
measured C701 = 0.18 µF and estimated L_tank ≈ 250 µH:

- Calculated LC resonance f_r = 1 / (2π√(LC)) = **23.7 kHz**
- Above f_r: ZVS achieved, IGBTs survive
- Below f_r: hard switching (capacitive mode), IGBTs die in milliseconds

**The 21.2 kHz and 22.4 kHz observations are BELOW the predicted f_r.**

This is the exact failure mechanism the SPICE simulation predicted:
when the controller is hunting and lands below f_r, an installed IGBT
(Q701 specifically — the low-side switch) would see hard switching at
full bus voltage. Result: catastrophic dissipation in the device, leading
to the symmetric C-E-G short pattern observed in the original failure.

### What this DOESN'T tell us

We don't know if the same hunting behavior would occur on a healthy
factory board with the design-intent component values. Two possibilities:

1. **Normal behavior:** All Panasonic boards hunt like this at startup
   without load, but the controller's internal protection (current limit,
   minimum f_sw clamp, or dV/dt sensing) protects the IGBTs during the
   brief moments below f_r. Healthy boards settle quickly once a magnetron
   provides load feedback.

2. **Frankenboard-specific:** Our Frankenboard has component values
   (especially C701 = 0.18 µF on this revision vs 0.1 µF on the older
   Panasonic schematic) that put the LC resonance in a place the
   controller's hunting range bottoms out below f_r. Other boards would
   not show this pattern.

We cannot distinguish between these without a known-good factory board
to compare against.

## Conclusion

The Frankenboard remains a **DO NOT POWER WITH IGBTS INSTALLED** candidate
until either:

a) A factory board comparison shows this hunting is normal, in which case
   we know the IGBTs survive briefly-below-f_r operation by design.

b) The hunting on a factory board converges to a stable above-f_r operating
   point quickly enough that the brief sub-f_r excursion isn't destructive.

Either way: **first fire test on a fresh factory board, not the Frankenboard.**

## Status signal correlation

The absence of status signal on CN701 pin 1 is **separately informative**.
It confirms:

- The previous firmware's bug (driving 110 Hz onto pin 3) was creating a
  signal where the ASIC's own status logic intended none to exist
- Without that corruption, the status line is correctly silent at idle
- The new firmware's status-gated startup will correctly ABORT when no
  status appears — which is the safe behavior

## Captured scope frames

See companion JPGs in this folder:
- `2026-05-30_frankenboard_dry_run_29kHz.jpg` — 29.3 kHz, ZVS region
- `2026-05-30_frankenboard_dry_run_33kHz.jpg` — 32.8 kHz, ZVS region  
- `2026-05-30_frankenboard_dry_run_21kHz_DANGER.jpg` — 21.2 kHz, BELOW f_r
- `2026-05-30_frankenboard_dry_run_22kHz_DANGER.jpg` — 22.4 kHz, BELOW f_r

All frames show DC-coupled, 10 V/div, 5 µs/div on the DS213.
