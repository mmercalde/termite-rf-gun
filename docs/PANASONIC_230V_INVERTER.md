# Panasonic 230V Dual-IGBT Inverter — Reference Notes

Project board: **F6645M301GP** (and related: F6645M300GP, F6645M303GP,
F6645M305GP, F6645M306GP, F6645M302BP). Closely related family with shared
architecture: **F606YM300BP** (NN-ST657S etc.) and **A606YM30** (Hungarian
forum references). All are 230V dual-IGBT half-bridge inverters from
Panasonic Home Appliances Microwave Oven (Shanghai) Co., Ltd., circa 2006-2010.

This document captures what we know about these boards from publicly
available sources, since Panasonic deliberately does not publish inverter
schematics in service manuals (treated as non-serviceable subassemblies).

---

## Why no FCC schematic exists

FCC certification is **US-market only**, and the US is 120V. Every Panasonic
inverter board in the FCC schematic database is therefore a **120V
single-IGBT** design. The closest public document (FCC ID ACLAP4T01,
single-IGBT NNS540/NNS560-era) shows the analog ASIC (IC801) controller
architecture but not the dual-IGBT topology.

230V dual-IGBT boards never go through FCC certification because they're
not sold in the US. The European/Asian regulatory paths (CE, VDE, BSI,
IEC, CCC) do not require public schematic disclosure.

**Conclusion:** there is no *official* schematic for F6645M301GP or any
other 230V dual-IGBT Panasonic inverter. However, a community-sourced
reverse-engineered schematic of a Panasonic universal-input (110V/220V)
dual-IGBT inverter has been located and archived in the repo at:

**`docs/reference/Panasonic_Inverter_Schematic_Annotated.pdf`**

This shows the same dual-IGBT half-bridge topology and labels all key
components by reference designator and value. See the [Component values
from the archived schematic](#component-values-from-the-archived-schematic)
section below for extracted values.

---

## What's actually in the public service manuals

### NN-ST657S / NN-ST677M / NN-ST667W / NN-ST657W (2007, Australia/NZ 230V)

Service manual order number PHAMOS0706032A3. The closest published Panasonic
service document to our F6645M301GP. Inverter board is **F606YM300BP** —
dual-IGBT, same architectural family.

- Browser viewer: https://www.manualslib.com/manual/1871563/Panasonic-Nn-St657s.html
- Page 5: oven-level schematic only (inverter shown as black box)
- Page 14: semiconductor ohmmeter check table + inverter parts list
- The inverter internal schematic is **NOT included** in the manual

### F606YM300BP main parts list (from NN-ST657S page 14)

| Ref | Part No. | Description | Notes |
|-----|----------|-------------|-------|
| **Q701** | A691EM300BP | IGBT | Main switch (big) |
| **Q702** | *(blank)* | IGBT | Smaller switch — no Panasonic P/N |
| **C701** | ECWF5184N300 | Film capacitor | **Resonant tank cap — critical** |
| C702 | ECQE2505T869 | Film capacitor | DC bus / snubber |
| C703 | ECWF2395N632 | Film capacitor | |
| DB701 | B0FBBS000001 | Rectifier bridge | Mains bridge |
| L701 | F5020M300GP | Choke coil | Mains filter |
| R702 | D0CM352JA002 | Sand bar resistor | 15W wirewound |
| **T701** | A609AM300GP | Transformer assembly | **Includes D701, D702, C706, C707** — HV doubler built into transformer assy, not separately replaceable |
| D701, D702 | B0FBAZ000001 | HV diodes | Part of T701 assembly |
| C706, C707 | ECWH3xxx | HV film caps | Part of T701 assembly |

### Semiconductor ohmmeter test table (NN-ST657S page 14)

For testing the IGBTs in or out of circuit:

| Pin pair | Forward (red→first listed) | Reverse |
|----------|---------------------------|---------|
| E–C | SMALL (body diode 0.4-0.7V) | ∞ |
| E–G | ∞ | ∞ |
| C–G | ∞ | ∞ |

Use this to verify new IGBTs before installation and to test suspected
damage on existing boards.

---

## Component values from the archived schematic

Extracted from `docs/reference/Panasonic_Inverter_Schematic_Annotated.pdf`
(community reverse-engineering of a universal-input 110V/220V board with
the same dual-IGBT topology as F6645M301GP).

### Half-bridge IGBT topology

The schematic confirms **dual-IGBT half-bridge** architecture:

- **Q701** (low-side, bottom switch): G60N321 in the reference schematic
  (our boards use GT50J327; both are same TO-247 series-resonant family)
  - Gate driven by Q703/Q704/Q705 totem-pole (C2785 NPN + A1174 PNP) direct from +20V rail
  - Simpler gate drive because it's ground-referenced
- **Q702** (high-side, top switch): GT30J322 in the reference schematic
  (our boards use GT35J321; both are same series-resonant family)
  - Gate driven through R707-709 (33kΩ bleed) + R710-712 (36+11+36) + ZD703/ZD704 clamps
  - More elaborate gate drive because it's high-side / level-shifted

The asymmetry in gate drive networks explains the asymmetry in IGBT
current ratings — high-side carries different di/dt and dv/dt stress than
low-side, hence the smaller current rating on Q702.

### Key component values (reference design)

| Ref | Value | Role |
|-----|-------|------|
| **C701** | **0.68 µF / 500 V** | **Resonant tank capacitor** — critical for resonant frequency |
| **C702** | 4 µF / 250 V | DC bus filter (deliberately small per VK3HZ) |
| C703 | (small film) | Snubber |
| **C704, C705** | 8200 pF / 3 kV | HV doubler capacitors (inside T701 assembly) |
| C707 | 470 µF / 25 V | +20V rail bulk cap |
| L701 | (line choke) | Mains filter |
| CT701 | Current transformer | Input current sense → constant-power loop |
| **R715-R717** | **4.5 kΩ 15 W** | Sand-bar wirewound bias supply (runs hot in normal operation) |
| R722 | 270 Ω | (Bias network) |
| R723, R721, R718, R720 | 201K, 241K, 180K, 241K | Voltage divider, AC sense |
| R725 | 200 kΩ | (Sense pull-up) |
| R702 | 15 kΩ | Gate-emitter clamp on Q701 (not the sand-bar) |
| D704, D705 | 6.2 V (A6V71) | Low-voltage detect zeners |
| ZD701, ZD702, ZD705 | (zeners) | Bias rail clamps |
| ZD703, ZD704 | (zeners) | Q702 gate clamp |
| D706 | LED + diode | Status feedback to IC701 |
| R732 | 11 kΩ | IC702 LED current limit |
| R733 | 1.5 kΩ | IC701 phototransistor pull-up |

### Note on F606YM300BP/F6645M301GP value differences

The archived schematic shows a universal-input board. Our 230V-only
F6645M301GP family may use different specific values, particularly:

- **C701 resonant cap**: F606YM300BP parts list shows ECWF5184N300 which
  decodes to ~0.18 µF. The universal-input reference shows 0.68 µF /
  500V. Different sizing optimizes for the operating voltage/frequency
  point — but the *role* is identical.
- **Bridge rectifier**: 230V boards need higher voltage rating
- **Mains caps**: 230V rated parts

Use the archived schematic to understand **topology and signal
relationships**, but verify specific values against the actual board
when needed.

### Controller IC functional blocks

The custom Panasonic analog ASIC (no public part number for the dual-IGBT
generation) handles these functions, labeled in the schematic:

1. **Power supply circuit low-voltage detect** (pins 2, 3, 5, 6) —
   monitors mains/DC bus, prevents operation below threshold (this
   is what caused trouble during 120V testing of the 230V boards)
2. **Power control** (pins 7, 8) — accepts the duty-cycle command
3. **Switching control** (pins 9, 15, 13, 4) — generates internal 24-40 kHz
   IGBT drive signals
4. **Feedback signal circuit** — receives current/voltage sense, drives
   the constant-power regulation loop
5. **Start control** (pins 10, 11, 12) — handles the cold-start sequence
   and decides when to assert the 110 Hz status signal
6. **CN701 interface** (pins 14, 13, 1, 4 → IC701/IC702 optos) — opto-
   isolated boundary to the DPC

---

## CN701 interface (definitive)

3-pin connector from inverter board to Digital Programmer Circuit (DPC).
Pin roles confirmed by VK3HZ canonical reverse-engineering. Pin numbering
on F6645M301GP per our bench measurements:

| Pin | Color  | Role | Driven by | Signal |
|-----|--------|------|-----------|--------|
| 1   | Yellow | Command  | DPC (or our ESP32) | 220-222 Hz square wave, variable duty = power setpoint |
| 2   | Brown  | GND      | Common | Opto-isolated ground reference |
| 3   | Orange | Status   | **Inverter** | 110 Hz / 50% square wave, present only when magnetron is drawing current |

Critical: pin 3 is INPUT to the controller, not output. The inverter
generates the 110 Hz status signal internally and the controller monitors
it. Our firmware must NOT drive this line — it must read it. See
`VK3HZ_FINDINGS.md` for the startup sequence implications.

---

## Service-manual sources for related Panasonic dual-IGBT boards

These can all be downloaded from ManualsLib (free, may want signup) or
elektrotanya. None contains the actual inverter schematic, but several
have useful test procedures, troubleshooting flows, and parts data.

| Manual | Models | Inverter board | Pages | Notes |
|--------|--------|----------------|-------|-------|
| NN-ST657S service | ST657S/ST677M/ST667W/ST657W | F606YM300BP | 27 | Best reference for F6645M family (supplement to NN-S560WF). [Link](https://www.manualslib.com/manual/1871563/Panasonic-Nn-St657s.html) |
| NN-ST678S service | ST678S | similar | 40 | Full standalone manual |
| NN-ST681S service | ST681S | similar | 42 | Full standalone manual |
| NN-SD697S service | SD697S | similar | 35 | |
| NN-SD698S service | SD698S | similar | 40 | |
| NN-GD376S service | GD376S/GD366M/GD356W | F606YM300BP | 31 | Microwave/grill, 2006. [Link](https://www.manualslib.com/manual/801129/Panasonic-Nn-Gd376s.html) |
| NN-SD798S service | SD798S | F606Y8M00AP (single-IGBT) | — | LATIN AMERICA 120V variant. Architecture-similar, single-IGBT. [Direct PDF](https://www.csportal.panasonic-la.com/DESCARGASPLA/PLA/ELECTRODO/MICROONDAS/INVERTER/NN-SD798SRPH/MANUAL%20DE%20SERVICIO/sd798s_rph.pdf) |

---

## Reference / community sources

**VK3HZ reverse-engineering** (canonical, captured in `VK3HZ_FINDINGS.md`):
- http://www.vk3hz.net/amps/Microwave_Oven_Inverter_HV_Power_Supply.pdf
- Docplayer mirror (browser-viewable): https://docplayer.net/21544207-Panasonic-microwave-oven-inverter-hv-power-supply.html

**FCC schematics (120V single-IGBT only — for architectural reference):**
- ACLAP4T01 (NNS540/NNS560): https://fccid.io/ACLAP4T01/Schematics/Inverter-Power-Supply-Schematic-77643
- ACLAP7A01 (NN-P994SFR, 2007): https://fccid.io/ACLAP7A01 — confirmed single-IGBT
- ACLAP5H01: https://fccid.io/ACLAP5H01 — older single-IGBT
- ACLAPCB01 (NN-GN68KS, 2019): https://fccid.io/ACLAPCB01 — modern single-IGBT

**YouTube reverse-engineering:**
- tomtechtod9200 F66459X91AP H97 inverter (sister board to ours):
  https://www.youtube.com/watch?v=LZb6v0JMOuU
  Note: this video confirms 220 Hz command signal and lists related boards
  (F606Y8X00AP, F606YM300BP, F606Y6G00CP, F606Y8X04AP) as sharing the same
  architecture.

**Forum threads:**
- Hungarian elektrotanya — F606YM30 / F6645 IGBT replacement discussion
  (mentions GT35J321 + GT50N322 originals): https://elektrotanya.com/panasonic_microwave_inverter.pdf/download.html
- High Voltage Forum repair thread:
  https://highvoltageforum.net/index.php?topic=1639.0
- Fusor.net thread (fusion enthusiasts using these as HV supplies):
  https://fusor.net/board/viewtopic.php?t=4820
- AllAboutCircuits inverter-microwave-oven thread:
  https://forum.allaboutcircuits.com/threads/inverter-microwave-ovens.143728/

**LG patent (alternate-architecture reference, not Panasonic):**
- US6936803B2, US7064306B2 — LG dual-IGBT inverter patents, useful for
  understanding the closed-loop current regulation pattern common across
  the consumer inverter industry.

---

## Original IGBTs in F6645M301GP

| Position | Toshiba Original | Class | Status |
|----------|------------------|-------|--------|
| Q701 (big) | GT50J327 | 600V, 50A, 4th-gen, current-resonance inverter family | Obsolete |
| Q702 (small) | GT35J321 | 600V, 35A, current-resonance inverter family | Obsolete |

Both Toshiba parts are obsolete and not stocked at mainline distributors
(Mouser, Digi-Key). Replacements must be either pulled from donor boards
or substituted with modern equivalents.

## Infineon TRENCHSTOP RC-H5 replacements (Mouser-stocked, current production)

| Position | Replacement | Class | Notes |
|----------|-------------|-------|-------|
| Q701 (big) | **IHW40N120R5** | 1200V, 40A nominal (80A pulse), TO-247 | https://www.infineon.com/part/IHW40N120R5 |
| Q702 (small) | **IHW30N120R5** | 1200V, 30A nominal (60A pulse), TO-247 | Smaller die from same family |

### Parameter comparison (Toshiba originals vs. Infineon replacements)

| Parameter | GT50J327 | IHW40N120R5 | GT35J321 | IHW30N120R5 |
|-----------|----------|-------------|----------|-------------|
| VCES (max) | 600 V | **1200 V** (2× headroom) | 600 V | **1200 V** |
| IC continuous | 50 A | 40 A nominal (80 A peak) | 35 A | 30 A nominal (60 A peak) |
| VCE(sat) typ | 2.1 V | **1.55 V** (lower loss) | 2.0 V | 1.55 V |
| Fall time tf | 250-300 ns | **20 ns** (10× faster) | ~250 ns | ~20 ns |
| Gate threshold | ~5-6 V | 5.8 V | ~5-6 V | ~5.8 V |
| Body diode | Yes, integrated FRD | Yes, monolithic | Yes | Yes |
| Tj(max) | 150°C | 175°C | 150°C | 175°C |
| Package | TO-247 | TO-247 | TO-247 | TO-247 |
| Pinout | G-C-E | G-C-E | G-C-E | G-C-E |
| Family | "Current Resonance Inverter Switching" | "Resonant Switching RC-H5" | Same | Same |

The Infineon parts are the right family for this application
(resonant-switching inverters, not motor drives), drop-in mechanically,
and have substantial headroom in every direction (V, I, T). The only
meaningful behavioral difference is faster switching (~10× faster fall
time), which is generally beneficial but could in some cases warrant a
small gate resistor (5-22Ω series) to dampen edge ringing.

---

## Failure modes observed on these boards (literature + our experience)

1. **Both IGBTs blown** — most common failure. Almost always cascades
   bridge rectifier and/or gate driver components. Treat board as
   suspect even after IGBT replacement.
2. **HV diode in T701 shorted** — kills magnetron oscillation, can stress
   IGBTs. Cannot replace separately, must replace whole T701 assembly.
3. **C701 (resonant cap) degraded** — silent killer. Shifts resonant
   frequency, makes inverter off-resonance, destroys new IGBTs almost
   immediately. Always measure capacitance and ESR on suspect boards.
4. **R702 (sand-bar 15W) drift open** — bias/clamp resistor, hot in
   normal operation, prone to drift after years of use.
5. **DB701 bridge rectifier shorted** — sometimes primary failure,
   sometimes downstream from IGBT short-circuit event.

## Panasonic service-bulletin precedent for IGBT upgrades

Per VK3HZ findings, Panasonic themselves issued an IGBT upgrade kit for
the single-IGBT NN-S550WF family. The kit replaced:
- Q701: GT60N90 (900V) → **GT60N321 (1000V)** — higher voltage class
- Plus a control-board capacitor change: **330 pF → 56 pF**

This proves three things relevant to us:
1. The original IGBTs were marginal even by Panasonic's standards
2. Upgrading to higher voltage class is a valid factory-approved approach
3. Gate-timing capacitor tuning may need to follow IGBT speed changes
   (worth keeping in mind if our 10×-faster Infineon parts cause issues)

---

## What we still do NOT have

- Actual Panasonic-issued schematic of F6645M301GP specifically (closest
  is the community-reverse-engineered universal-input schematic now
  archived in `docs/reference/`)
- Schematic of the IC801-equivalent controller IC on the dual-IGBT
  generation (the functional blocks are labeled but the internal die
  is proprietary)
- Specific F6645M301GP resonant tank operating frequency (estimated
  24-40 kHz per LG patents and VK3HZ universal-input data, but exact
  frequency unconfirmed for our boards)
- Definitive part number for Q702 in F606YM300BP parts list (Panasonic
  left it blank in their own documentation)
- Snubber component values for the 230V variant specifically

These would have to be reverse-engineered from a known-good physical
board if we ever need definitive answers.

---

## Document history

- 2026-05-30 Initial creation, gathered after IGBT failures on
  Frankenboard built from two damaged donor boards.
- 2026-05-30 Added archived community schematic reference (universal-input
  dual-IGBT), extracted component values and gate-drive topology details.
  Confirmed both Q701 and Q702 are power IGBTs in half-bridge configuration.

