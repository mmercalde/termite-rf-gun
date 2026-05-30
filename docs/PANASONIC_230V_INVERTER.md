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

**Conclusion:** there is no public schematic for F6645M301GP or any other
230V dual-IGBT Panasonic inverter. The closest reference is the F606YM300BP
service-manual data (parts list + ohmmeter check tables, but no schematic).

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

## What we do NOT have

- Actual schematic of F6645M301GP (or any 230V Panasonic dual-IGBT board)
- Schematic of the IC801-equivalent controller IC on these boards
- Documented gate-drive network values for the dual-IGBT half-bridge
- Snubber component values
- Resonant tank operating frequency specification (estimated 24-40 kHz
  per LG patents and VK3HZ for single-IGBT, but Panasonic dual-IGBT
  may differ)
- Definitive part number for Q702 (Panasonic only lists Q701 in the
  F606YM300BP parts list — Q702 entry is blank)

These would have to be reverse-engineered from a known-good physical
board if we ever need definitive answers.

---

## Document history

- 2026-05-30 Initial creation, gathered after IGBT failures on
  Frankenboard built from two damaged donor boards.

