# VK3HZ Reverse-Engineering Findings — Panasonic Microwave Inverter HV PSU

Captured 2026-05-30 from David Smith VK3HZ's original PDF
(http://www.vk3hz.net/amps/Microwave_Oven_Inverter_HV_Power_Supply.pdf).

Hosted locally in repo because the canonical URL is intermittently unavailable.

Source PSU: Panasonic NN-S550WF (circa-2000, single-IGBT family).
The findings apply across the Panasonic inverter family — F606Y-series
(single-IGBT) and F6645-series (dual-IGBT, 240V) share the same DPC↔inverter
command interface architecture per author's note and the
tomtechtod9200 YouTube reverse-engineering of F66459X91AP.

---

## Architecture summary

- High Voltage PSU: 165×105×60 mm, 650 g
- Mains rectifier + IGBT switching at ~30 kHz internal frequency
- HV transformer drives half-wave doubler rectifier → ~4 kV DC at 300 mA
- HV filter caps only 8200 pF each (effectively 4100 pF in the doubler)
- Mains filter cap is small (~4 µF) — DELIBERATE design choice; 100 Hz ripple
  on the HV makes the magnetron stop oscillating during low-voltage portions
  of the line cycle, which helps spectrum sharing with WiFi etc.
- Output is **constant-POWER regulated**, not constant-voltage. The board
  senses input current internally and adjusts switching duty to deliver
  the commanded power. At a given P-level, output power stays within ~1 W
  across a 50% variation in load current.

## CN701 interface (3-pin DPC connector)

Two opto-isolated lines + ground:

| Pin | Role | Notes |
|-----|------|-------|
| Command | DPC → Inverter | TTL-level 220 Hz square wave; duty cycle = power setpoint |
| Status  | Inverter → DPC | 110 Hz square wave, 50% duty, **only present when magnetron is drawing current** |
| GND     | Opto-isolated common | Logic ground reference |

(Pin numbering varies between board generations; see project notes for the
F6645M301GP specifically. The roles are the constant.)

The control and status signals are synchronized to each other but unrelated
to AC mains frequency or phase.

Signal levels: TTL (2-3 V observed on a meter for "on" state because of
duty averaging — actual peaks are 5 V).

## Canonical command duty cycle table (KEY)

This is the table the DPC sends per power level. **First number is steady-state,
second number applies during the warm-up phase before status appears.**

| Power | On time (ms)  | Duty (%)     | Cycling                     |
|-------|---------------|--------------|-----------------------------|
| P10   | 1.5 / 1.1     | 33 / 25      | 4 s on / 18 s off           |
| P20   | 1.5 / 1.1     | 33 / 25      | 11 s on / 11 s off          |
| P30   | 1.5 / 1.1     | 33 / 25      | 18 s on / 4 s off           |
| P40   | 1.5 / 1.1     | 33 / 25      | Continuous                  |
| P50   | 1.5           | 33           | Continuous                  |
| P60   | 1.8           | 40           | Continuous                  |
| P70   | 2.5           | 55           | Continuous                  |
| P80   | 2.8           | 62           | Continuous                  |
| P90   | 3.1           | 69           | Continuous                  |
| **P100** | **3.4**    | **75**       | Continuous                  |

**P100 = 75% duty. Anything above 75% is over-driving the inverter.**

Period = 1/220 Hz ≈ 4.55 ms. So "1.5 ms on" out of 4.55 ms ≈ 33% duty.

## Startup sequence (CRITICAL — this is what we were missing)

For low-power settings (P10-P40), the DPC's behavior on START:

1. **Warm-up phase**: send a HIGHER duty than the requested power level
   (specifically, 25-33% duty / 1.1-1.5 ms on) to bring the magnetron
   filament up to operating temperature.

2. **Wait for status signal**: monitor the inverter's 110 Hz status output.
   The inverter only emits this signal when it detects current flowing —
   i.e., when the magnetron is warm and oscillating.

3. **Drop to requested power**: once status is seen, the DPC drops the
   command duty cycle to the requested P-level.

For higher power levels (P50+), the duty is continuous at the same value
throughout — but the DPC still waits for status confirmation before
declaring "running."

## Why the constant-power architecture is dangerous to drive open-loop

The inverter's internal control loop tries to deliver the commanded power
to the load. If there's no magnetron, no load, or the resonant tank is
off-resonance:

- The inverter sees insufficient current draw
- Its loop concludes "I need more power"
- It increases internal switching duty to compensate
- Without bounded protection, this can drive the IGBTs into shoot-through
  or off-resonance overcurrent
- The smaller IGBT typically fails first (lower current rating)

This is why **status-gated operation matters**: the DPC's job is to ensure
the inverter is actually running into a proper load before committing to a
power level. Bypass that gate and the inverter happily destroys itself.

## Quoted measured output (NN-S550WF specifically)

Output voltage/current measurements from VK3HZ's bench testing:

| Power | On (ms) | Min Load V/I/P    | Max Load V/I/P    |
|-------|---------|-------------------|-------------------|
| 40%   | 1.1     | 3730 V / 147 mA / 548 W | 2560 V / 210 mA / 538 W |
| 50%   | 1.5     | 3890 V / 175 mA / 681 W | 2720 V / 250 mA / 680 W |
| 60%   | 1.8     | 3980 V / 192 mA / 764 W | 2820 V / 275 mA / 776 W |
| 70%   | 2.5     | 4200 V / 240 mA / 1008 W | 3020 V / 330 mA / 997 W |
| 80%   | 2.8     | 3650 V / 300 mA / 1095 W | 3020 V / 365 mA / 1102 W |
| 90%   | 3.1     | 3930 V / 300 mA / 1179 W | 3080 V / 385 mA / 1186 W |
| 100%  | 3.4     | 4120 V / 300 mA / 1236 W | — (current limited) |

Note constant-power behavior: at P50, current can vary ~50% but output
power varies only 1 W.

Minimum continuous output is ~550 W (P40). For lower average power, the
DPC cycles P40 on/off over a 22-second window. The inverter itself
cannot run below P40 continuously.

## Original Panasonic IGBT modification (Service bulletin)

The original NN-S550WF had:
- Q701 (main switch): GT60N90 — 900 V / 60 A
- Q702 (smaller switch): GT30J322

Panasonic issued a service modification kit replacing:
- GT60N90 → **GT60N321** (1000 V vs 900 V — higher voltage headroom)
- GT30J322 → unchanged but supplied (likely indicating post-blowup replacement)
- **330 pF cap on control board → 56 pF**

Translation: Panasonic acknowledged the original IGBT was undersized AND
that gate timing needed adjustment. The control-cap change (~6× smaller
capacitance) means faster gate switching.

For your F6645M301GP (240V dual-IGBT), the originals are GT50J327 +
GT35J321 — same architectural pattern but higher-voltage class. Modern
Infineon TRENCHSTOP RC-H5 replacements (IHW40N120R5 + IHW30N120R5) are
even better — 1200 V, designed for soft-switching inverters.

## Implications for our standalone firmware (TL;DR)

1. **Send 50% duty during warmup, not the requested run duty.**
2. **WAIT for the 110 Hz status signal from the inverter before going to RUN.**
   Without status confirmation, the magnetron isn't oscillating; driving
   the inverter blindly is what destroys IGBTs.
3. **Hard-cap maximum duty at 75%** — anything above is above-spec.
4. **Don't drive a fake "feedback" signal onto the status line.** The
   inverter generates that signal; we read it, we don't write it.
5. **A magnetron load must be physically connected.** No-load operation
   confuses the constant-power regulator.

These are exactly the changes in firmware commit `3128125`.

## Sources

**Primary references (archived in-repo, used as authoritative):**

- **`docs/reference/Panasonic_ServiceCD_Schematic.png`** — Panasonic's own
  schematic of the HV inverter PSU from the Panasonic Service CD. Most
  authoritative non-proprietary reference for the topology and component
  values. Reproduced in the VK3HZ PDF below.
- **`docs/reference/Microwave_Oven_Inverter_HV_Power_Supply.pdf`** —
  David Smith VK3HZ's writeup including the Panasonic schematic plus
  bench measurements of operational behavior (the 220 Hz / 110 Hz /
  30 kHz signal characterization, power tables, constant-power analysis).

**Supplementary references (lower priority):**

- `docs/reference/Panasonic_Inverter_Schematic_Annotated.pdf` — Russian
  community reverse-engineering. Contains some errors; use only when
  Panasonic schematic doesn't have enough detail.

**Original online sources** (for citation; project doesn't depend on
these staying online):

- VK3HZ PDF: http://www.vk3hz.net/amps/Microwave_Oven_Inverter_HV_Power_Supply.pdf
- Docplayer mirror: https://docplayer.net/21544207-Panasonic-microwave-oven-inverter-hv-power-supply.html
- electronicshelponline blog: https://electronicshelponline.blogspot.com/2018/11/panasonic-microwave-oven-nn-s550wf.html
- tomtechtod9200 YouTube (F66459X91AP RE): https://www.youtube.com/watch?v=LZb6v0JMOuU
- Panasonic Tech Guide for Inverter Microwaves: https://media.datatail.com/docs/manual/371449_en.pdf

**Related Panasonic service manuals** (no schematic, but useful for
test procedures and parts cross-reference):

- NN-SD798S (Latin America 120V single-IGBT F606Y8M00AP):
  https://www.csportal.panasonic-la.com/DESCARGASPLA/PLA/ELECTRODO/MICROONDAS/INVERTER/NN-SD798SRPH/MANUAL%20DE%20SERVICIO/sd798s_rph.pdf
- NN-GD376S (230V dual-IGBT F606YM300BP):
  https://www.manualslib.com/manual/801129/Panasonic-Nn-Gd376s.html
- NN-ST657S (closest match for F6645M301GP family):
  https://www.manualslib.com/manual/1871563/Panasonic-Nn-St657s.html

**Attribution:** The VK3HZ document is copyright David Smith VK3HZ
(vk3hz [at] wia.org.au). The community schematic source is unknown
(Russian-annotated). Both are archived here for project continuity
under fair-use reference purposes; cite the originals in any
publication or derivative work.
