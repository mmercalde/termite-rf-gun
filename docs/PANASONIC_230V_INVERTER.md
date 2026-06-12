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

**Conclusion:** there is no *official Panasonic-published* schematic for
F6645M301GP specifically, but two schematics of similar dual-IGBT inverter
generations are archived in the repo:

1. **PRIMARY: `docs/reference/Panasonic_ServiceCD_Schematic.png`** — taken
   from the Panasonic Service CD (via VK3HZ's PDF). Shows full component
   detail except the proprietary controller ASIC. This is the authoritative
   reference.

2. **Supplementary: `docs/reference/Panasonic_Inverter_Schematic_Annotated.pdf`** —
   community reverse-engineering of a universal-input variant. Useful for
   cross-reference but contains some errors relative to the Panasonic
   schematic. Use only when the Panasonic schematic doesn't provide enough
   detail.

Both show the same dual-IGBT half-bridge topology as F6645M301GP. See the
[Component values](#component-values--panasonic-service-cd-schematic--bench-measurements)
section below.

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

## Component values — Panasonic Service CD schematic + bench measurements

**Primary reference:** `docs/reference/Panasonic_ServiceCD_Schematic.png`
(Panasonic's own schematic from the Panasonic Service CD, reproduced in
VK3HZ's writeup). This supersedes the Russian community reverse-engineering
which contained some errors.

### Half-bridge IGBT topology

The schematic confirms **dual-IGBT half-bridge** architecture:

- **Q702** (top IGBT, high-side switch)
  - Gate driven through R710 + R711 + R707 (36Ω each) + R712 + R713 (11Ω) +
    R714 (11Ω 1/2W) + ZD703, ZD704 clamps
  - R708, R709 (33kΩ each) as gate bleed/clamp
  - Elaborate gate drive because high-side requires level-shifting
- **Q701** (bottom IGBT, low-side switch)
  - Gate driven by Q703/Q704/Q705 totem-pole (NPN/PNP/NPN complementary pair)
  - R704 (10Ω) inline gate resistor
  - R705 (10kΩ) gate-emitter pulldown
  - Simpler gate drive because it's ground-referenced

The asymmetric gate drive networks (more complex for Q702, simpler for Q701)
are why Panasonic uses asymmetric IGBT current ratings — high-side and
low-side see different di/dt and dv/dt stress.

### Component values from Panasonic Service CD schematic

| Ref | Value | Role |
|-----|-------|------|
| L701 | (line choke) | Mains filter |
| DB701 | Bridged Diode | Mains rectifier |
| C702 | 4 µF | DC bus filter (deliberately small per VK3HZ) |
| CT701 | Current transformer | Input current sense → constant-power loop |
| R702 | 15 kΩ | Sense divider |
| R708 (one occurrence) | 10 kΩ | Sense divider |
| R704 | 10 Ω | Q701 gate inline resistor |
| R705 | 10 kΩ | Q701 gate-emitter pulldown |
| R707, R710, R711 | 36 Ω each | Q702 gate resistor network |
| R713, R714 | 11 Ω each | Q702 gate network |
| R708, R709 | 33 kΩ each | Q702 gate bleed |
| C701 | 0.1 µF | Across IGBTs (snubber/resonant) |
| C703 | 0.45 µF | Tank cap area |
| C706, C706E | 0.4 µF each | Tank cap |
| ZD703, ZD704 | (zeners) | Q702 gate clamp |
| D703 | (diode) | Sense/clamp |
| T701 | HV transformer | Step-up + bias winding |
| D701, D702 | UX-C2B (HV diodes) | HV doubler |
| C704, C705 | 8200 pF each | HV doubler caps (schematic value) |
| R701 | 100 MΩ | HV output bleeder |
| CN703 | HV output 4000V/300mA | To magnetron anode |
| H701, H702 | — | Magnetron heater terminals |
| IC702 + D706 + R733 (1 kΩ) | Command opto | DPC → inverter |
| IC703 + R732 (11 kΩ) | Status opto | Inverter → DPC |

### F6645M301GP-specific deltas — Michael's actual board

Several component values on Michael's specific board differ from the
generic Panasonic Service CD schematic. These are documented in detail in
`docs/reference/README.md`. Summary:

| Ref | Schematic value | Michael's board | Status |
|-----|-----------------|-----------------|--------|
| C701 | 0.1 µF | **0.18 µF** (WFK 184J 500V) | Photo confirmed |
| C704 | 8200 pF | 8200 pF (DHC 822J 3000V) | Photo confirmed |
| C705 | 8200 pF | **5600 pF** (DHC 562J 3000V) | Photo confirmed — asymmetric |
| R702 | 15 kΩ | **3.5 kΩ 15W** (RYC-3 3K5J) | Photo confirmed |
| R701 | 100 MΩ | 100 MΩ (OL on meter) | Bench confirmed |
| Q701 | (TO-247 IGBT) | IHW40N120R5 | Infineon replacement |
| Q702 | (TO-247 IGBT) | IHW30N120R5 | Infineon replacement |

The asymmetric HV doubler caps and different C701 value indicate this is
a later revision than the schematic documented.

### Note on Russian reverse-engineering

The earlier-archived `Panasonic_Inverter_Schematic_Annotated.pdf` (Russian
community reverse-engineering) was used as primary reference in initial
project work. It contains some errors relative to the Panasonic Service CD
schematic:
- Q701/Q702 values for resonant cap shown as 0.68 µF (Panasonic says 0.1 µF;
  Michael's board has 0.18 µF — closer to Panasonic)
- Specific part numbers (C2785 NPN, A1174 PNP) for gate drivers were
  speculative; Panasonic shows generic transistor symbols
- Controller IC pin numbering (2-15) shown was speculative

Use the Russian schematic only as a supplementary reference. When values
disagree, trust the Panasonic Service CD schematic.

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

## CN701 interface

3-pin connector from inverter board to Digital Programmer Circuit (DPC).

**Pin roles per the Panasonic Service CD schematic:**

| Pin (Panasonic schematic) | Role | Driven by | Signal |
|---------------------------|------|-----------|--------|
| 3   | Command (input to inverter) | DPC (or our ESP32) | 220-222 Hz square wave, variable duty = power setpoint, 2-3V level |
| 2   | 0V (GND)                    | Common              | Opto-isolated ground reference |
| 1   | Status (output from inverter) | **Inverter ASIC** | 110 Hz / 50% square wave, present only when magnetron is drawing current, 2-3V level |

**Note on pin numbering vs wire colors:**

Earlier project documentation used a different pin numbering convention
(swapping pin 1 and pin 3 labels). The *physical wiring* on Michael's
build matches the Panasonic schematic correctly — GPIO4 drives the
inverter's command input, GPIO5 reads the inverter's status output.
Only the numerical labels in some docs needed correction; no physical
rewiring is required.

The colors-to-roles mapping on Michael's actual cable:
- **Yellow** wire = command (GPIO4 → inverter command input)
- **Brown** wire = GND (common)
- **Orange** wire = status (inverter status output → GPIO5)

If documentation elsewhere in this repo refers to "pin 1 = command" or
"pin 3 = status," that's the older convention. The Panasonic schematic
is authoritative going forward.

**Critical:** the status line (orange, GPIO5) is INPUT to the ESP32.
The inverter generates the 110 Hz status signal internally; our firmware
must NOT drive this line — it must read it. The previous firmware
revision (before commit `3128125`) drove this line, which corrupted the
inverter's internal feedback loop and contributed to IGBT failure. See
`VK3HZ_FINDINGS.md` for the startup-sequence implications.

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

## VR701 / VR1 — current-sense calibration trim (topology is generation-dependent)

**Bottom line:** VR701 is the factory power-calibration trim — bench-confirmed
on Michael's board (50k "503"). It scales a current-sense signal into the
controller IC; it is NOT a filament adjust.

**IMPORTANT CORRECTION (2026-06-12):** the sense *topology* is generation-
dependent and the earlier "no CT701 / it's a SHUNT R703" claim in this section
overstated what's actually documented. The record now:

- **Both schematics IN THIS REPO show a CURRENT TRANSFORMER (CT701)**, not a
  shunt, in the AC line between CN702 and the bridge rectifier DB701:
  - `Panasonic_ServiceCD_Schematic.png` (authority #2) — labels it explicitly
    "CT701 / CURRENT TRANSFORMER".
  - `Panasonic_Inverter_Schematic_Annotated.pdf` (Russian RE, authority #4) —
    also shows CT701 (secondary -> R715 4.5K/15W -> R716/R717 100R -> VR701/R719
    divider -> controller power-control pins 7/8). Note Russian shows VR701=1k,
    not 50k.
- **Michael's ACTUAL board has NO current transformer** — confirmed by visual
  inspection — and uses a **resistor-network sense instead.** This is consistent
  with it being a LATER REVISION (same board already shows later-gen deltas:
  0.18uF resonant cap, asymmetric 8.2nF/5.6nF doubler, 3.5K R702). Panasonic
  migrated this sense path from CT to a resistor network on newer boards.
- So: "CT701" is correct for the documented (older) generation; "resistor
  network, no transformer" is correct for Michael's (later) board. Both are
  true for their respective generations.

### What the part is
- Board silkscreen **VR1** = schematic **VR701**. White trimmer marked **"503"
  = 50 kOhm** (photo-confirmed, component side). Note this is a third value in
  the family: V-series doc = 1k, L/N-series doc = 10k, this board = 50k.
- Physical location: cold/control side, between CN702 (above) and CN701
  (below), in the small-signal cluster near the IC702/IC703 optos.
- Panasonic flags it **"DO NOT ADJUST VR701"** — see the annotated board photo
  on **page 7 of the NN-GD376S service manual** (manualslib id 801129). That
  photo is the best physical-location reference we have for VR701, better than
  either schematic. The "do not adjust" warning has one documented exception:
  adjustment IS required after the HV transformer (T701) is replaced.

### Sense topology on Michael's board — resistor network (NOT schematic-backed in repo)
Michael's board uses a **resistor-network current sense, no current transformer**
(visual inspection). Earlier revisions of this doc called the element "SHUNT R703
in the DC-bus return," citing the **tomtechtod9200 RE schematic** — but that
schematic is **NOT in this repo**, and the **two schematics that ARE in the repo
both show CT701** (see correction above). So the specific "R703, DC-bus return"
designation and location is **board-observed + external-RE inferred, NOT verified
against any in-repo schematic.** Treat the *resistor-network* fact as solid (seen
on the board); treat the *exact element ID, value, and tap point* as UNCONFIRMED
until the board trace below is done. VR701 scales whatever this network produces
before it reaches the controller.

### Controller IC
Named on the RE schematic as **AN47054A** (Panasonic analog ASIC, no public
datasheet). Its internal regulation behaviour is unknown; we model only the
passive front-end feeding it.

### Sense chain (as read; assumptions flagged)
    SHUNT R703 -> front-end -> Vsense
    Vsense --[2k4]-- nIn --[R40 (value TBC)]-- potHi
    VR701 50k:  potHi --(1-p)*50k-- wiper --(p)*50k-- GND
    wiper --[100k]-- controller sense pin (~pin 13)

### SPICE result (script NOT in repo — treat as hypothesis, not verified)
NOTE (2026-06-12): `spice/vr701_cal.py` and `spice/results/03_vr701_cal.png`
are CITED here but are **NOT present in the repo** (only `spice/ngspice_sweep.py`,
a tank frequency sweep, exists). The conclusions below were never reproducible
from committed files — treat them as a reasonable first-principles HYPOTHESIS to
confirm on the bench, not a verified simulation. Also: the model assumed a
specific sense front-end whose topology is now in question (see correction above),
so even the qualitative result needs re-checking against the real board network.
- Voltage at the controller sense pin is **linear** in wiper position ->
  VR701 is a linear gain control on the current-sense signal.
- If the chip regulates that pin to a fixed reference, the regulated current
  (= power) setpoint is a **1/x** curve vs VR701:
  - **more wiper -> chip reads more volts/amp -> backs off -> LOWER power**
  - **less wiper -> under-reads -> pushes harder -> HIGHER power**
- **Danger zone = low-wiper / high-power end** (curve is steep there; small
  moves swing current hard -> IGBT avalanche risk). High-wiper end is flat
  (fine, safe low-power control).
- Shape is robust to the unknowns; absolute numbers are NOT.

### Calibration procedure (after identical-T701 swap)
1. Mark the original wiper position before touching it. Identical part -> only
   tolerance moves -> expect a small tweak, not a full recal.
2. If disturbed/unknown: set to the LOW-power end first, power up at low DPC
   duty, confirm screwdriver direction vs power on the inline ammeter, then
   bring power UP slowly toward your target current. Small nudges near max.
3. Target = YOUR load's intended operating current at your chosen duty, NOT the
   oven's spec'd input current (meaningless for this application).

### Open items for bench confirmation
- **RESOLVING MEASUREMENT (do this first): trace VR701's wiper and both ends.**
  This single trace settles the whole topology question that neither in-repo
  schematic answers for this board:
  - If the sense input originates at a **low-value resistor in the rectified-DC
    return** -> shunt/resistor-network, bus-referenced (probing hazard; needs
    isolation to read).
  - If it originates at a **transformer/toroid in the AC line** (where CT701 sits
    on the schematics) -> CT-coupled and isolated after all.
  Result determines (a) shunt vs divider topology, (b) whether it's bus-
  referenced (safety), (c) which VR701 calibration physics actually applies.
- R40 actual value (if a resistor network), and VR701 wiring (divider vs
  rheostat; where the wiper truly returns).
- Whether the controller regulates this pin to a fixed reference, and its value.
- One real datapoint: measured input current at a known VR701 position + DPC
  duty, to pin the curve's absolute scale (also serves as the IGBT-current
  visibility the sweep firmware lacks).

- 2026-06-12 CORRECTED the VR701/sense section after reading both in-repo
  schematics directly. Both `Panasonic_ServiceCD_Schematic.png` (Panasonic,
  authority #2) and `Panasonic_Inverter_Schematic_Annotated.pdf` (Russian RE,
  #4) show **CT701 CURRENT TRANSFORMER** in the AC line — NOT a shunt. The
  earlier "no CT701 / SHUNT R703 in DC-bus return" claim is board-observed +
  external-RE (tomtechtod9200, not in repo) and is NOT backed by any in-repo
  schematic. Re-framed as: CT701 = documented/older generation; resistor-network
  (no transformer) = Michael's later board, confirmed only by visual inspection,
  exact element ID/value/tap point UNCONFIRMED. Flagged `spice/vr701_cal.py` as
  missing from repo (conclusions = hypothesis, not verified). Promoted "trace
  VR701 wiper + both ends" to the top resolving measurement.

## Earlier document history

- 2026-05-30 Initial creation, gathered after IGBT failures on
  Frankenboard built from two damaged donor boards.
- 2026-05-30 Added archived community schematic reference (universal-input
  dual-IGBT), extracted component values and gate-drive topology details.
  Confirmed both Q701 and Q702 are power IGBTs in half-bridge configuration.
- 2026-05-30 Switched primary reference from Russian community schematic to
  Panasonic Service CD schematic (more authoritative). Corrected CN701 pin
  numbering to match Panasonic's convention (pin 3 = command IN, pin 1 =
  status OUT). Noted physical wiring is correct per Panasonic schematic;
  only numerical labels in earlier docs were swapped. Added F6645M301GP-
  specific component value deltas observed via board photos.

- 2026-06-07 Added VR701/VR1 section. CORRECTED: no CT701 on these boards;
  current sense is SHUNT R703 (DC-bus return). VR701 = 50k "503" trimmer =
  current-sense GAIN calibration into AN47054A controller (named from RE
  schematic). Added SPICE model (spice/vr701_cal.py) showing linear sense gain
  -> 1/x power setpoint, polarity (more wiper = lower power), danger at
  low-wiper/high-power end. Added NN-GD376S p.7 annotated photo as VR701
  physical-location reference. Superseded earlier CT701-based loop description.
