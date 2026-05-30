# Reference Documents

Archived primary sources for the Panasonic F6645M301GP inverter project,
stored locally so the project doesn't depend on external sites.

## Authoritative reference hierarchy

When references disagree, this is the order of authority:

1. **Michael's bench measurements on the actual board** — ground truth
2. **`Panasonic_ServiceCD_Schematic.png`** — Panasonic's own published schematic
3. **`Microwave_Oven_Inverter_HV_Power_Supply.pdf`** — VK3HZ writeup including
   the bench-measured behavior of the same topology
4. **`Panasonic_Inverter_Schematic_Annotated.pdf`** — community reverse-
   engineering, useful for additional component identification but may
   contain errors or refer to a different board generation

The Russian reverse-engineering was used as primary reference in earlier
revisions of the project docs. Subsequent corrections moved to the
Panasonic Service CD schematic as primary. Some early docs may still
reference Russian-schematic values that don't match the actual board —
fixes are ongoing as discrepancies are found.

## Files

### `Panasonic_ServiceCD_Schematic.png` (PRIMARY — Panasonic-authored)

Panasonic's own schematic of the HV inverter PSU, originally from the
Panasonic Service CD and reproduced in VK3HZ's writeup. Shows the dual-IGBT
half-bridge topology with full component-level detail except the controller
ASIC (drawn as a labeled black box).

**Component values from this schematic:**

| Ref | Value | Role |
|-----|-------|------|
| L701 | (line choke) | Mains filter |
| DB701 | Bridged Diode | Mains rectifier |
| C702 | 4µF | DC bus filter (deliberately small per VK3HZ) |
| CT701 | Current Transformer | Input current sense |
| Q702 | (top IGBT) | High-side switch |
| Q701 | (bottom IGBT) | Low-side switch |
| R710, R711, R707 | 36Ω | Q702 gate resistor network |
| R712 | (small) | Part of Q702 gate network |
| ZD703, ZD704 | (zeners) | Q702 gate clamp |
| R713 | 11Ω | Q702 gate network |
| R714 | 11 1/2W | Q702 gate network |
| R708, R709 | 33kΩ each | Q702 gate bleed |
| R702 | 15kΩ | Sense divider top |
| R708 | 10kΩ | Sense divider bottom (listed twice — possibly two refs same value) |
| Q703 | (NPN) | Gate driver totem-pole NPN |
| Q704 | (PNP) | Gate driver totem-pole PNP |
| Q705 | (transistor) | Part of Q701 gate drive |
| R704 | 10Ω | Q701 gate inline resistor |
| R705 | 10kΩ | Q701 gate-emitter pulldown |
| D703 | (diode) | Sense/clamp |
| **C701** | **0.1µF** | **Across IGBTs (snubber + resonant element)** |
| **C703** | **0.45µF** | **Tank cap area** |
| C706 | 0.4µF | Tank cap |
| C706E | 0.4µF | Tank cap (separate reference) |
| T701 | Inverter HV Transformer | |
| P701, P702 | (primary terminals) | |
| S701, S702 | (secondary terminals) | |
| D701, D702 | UX-C2B (HV diodes) | HV doubler |
| **C704, C705** | **8200pF each** | **HV doubler caps** |
| R701 | 100MΩ | HV output bleeder |
| H701, H702 | — | Magnetron heater terminals |
| CN703 | HV output 4000V/300mA | To magnetron anode |

**CN701 (control connector) per Panasonic schematic:**
- **Pin 3**: Control signal INPUT (2-3V level) — command from DPC
- **Pin 2**: 0V (GND)
- **Pin 1**: Control signal OUTPUT (2-3V level) — status to DPC

This is the OPPOSITE pin labeling we'd been using in some docs. Michael's
physical wiring matches the Panasonic schematic (GPIO4 drives the inverter's
command input, GPIO5 reads the inverter's status output). Only the pin
*numbers* in some docs needed correction, not the actual circuit.

**Opto-isolators:**
- IC702 (with D706 in series, R733 = 1kΩ) = command opto (DPC → inverter)
- IC703 (with R732 = 11kΩ) = status opto (inverter → DPC)
- D706 is part of the command opto LED chain, NOT a separate status LED
  as the Russian schematic implied

**Controller ASIC** drawn as a single block with labeled functional sections:
- Power supply circuit low-voltage detect
- Power control
- Switching control
- Start control
- Feedback signal circuit

The internal pin numbering shown on the Russian schematic for the controller
IC was speculative — Panasonic doesn't publish ASIC internals.

### `Microwave_Oven_Inverter_HV_Power_Supply.pdf`

The canonical VK3HZ reverse-engineering writeup by David Smith VK3HZ
(vk3hz [at] wia.org.au). Includes the Panasonic Service CD schematic above
plus bench measurements, signal characterizations, and operational analysis.

Original URL:
http://www.vk3hz.net/amps/Microwave_Oven_Inverter_HV_Power_Supply.pdf

Mirrored at:
https://docplayer.net/21544207-Panasonic-microwave-oven-inverter-hv-power-supply.html

**Key facts established by VK3HZ via bench measurement** (not reverse-engineering):
- Control signal: 220 Hz square wave, variable duty cycle, TTL level (2-3V observed)
- Status signal: 110 Hz / 50% fixed, present when magnetron drawing current
- IGBT switching frequency: ~30 kHz internal
- 4 kV at 300 mA = 1200 W maximum output
- Constant-power regulation (1 W variation across 50% load change)
- 100 Hz ripple on HV from small C702 (4µF) — intentional design choice

### `Panasonic_Inverter_Schematic_Annotated.pdf`

Community-sourced reverse-engineering of a related Panasonic universal-input
inverter, with Russian and English functional-block annotations. Useful for:
- Confirming the controller ASIC's functional layout
- Additional discrete component identification
- Cross-reference for board generations

**Known discrepancies with the Panasonic Service CD schematic:**
- C701 listed as 0.68µF / 500V (Panasonic schematic shows 0.1µF)
- Q703 designated NPN with specific 2SC2785 part number (Panasonic schematic
  shows generic transistor symbols, exact P/N not specified)
- May reflect a different board generation or contain reverse-engineering errors

Use this as supplementary reference only when the Panasonic schematic doesn't
provide enough detail.

## F6645M301GP-specific deltas (Michael's actual board)

The board in this project is a specific revision with values that differ
from the generic Panasonic Service CD schematic:

| Ref | Service CD schematic | Michael's actual board | Source |
|-----|---------------------|------------------------|--------|
| C701 | 0.1µF | **0.18µF / 500V** (WFK 184J) | Photo, confirmed |
| C704 | 8200pF | 8200pF (DHC 822J 3000V) | Photo, confirmed |
| C705 | 8200pF | **5600pF** (DHC 562J 3000V) | Photo — asymmetric on this board! |
| R702 | 15kΩ | **3.5kΩ 15W** (RYC-3 3K5J) | Photo, confirmed |
| R701 | 100MΩ | 100MΩ (OL on meter) | Bench, confirmed |
| Q701 | (TO-247 IGBT) | IHW40N120R5 (Infineon TRENCHSTOP RC-H5) | Replacement choice |
| Q702 | (TO-247 IGBT) | IHW30N120R5 (Infineon TRENCHSTOP RC-H5) | Replacement choice |

The asymmetric HV doubler caps (8.2nF + 5.6nF) and different resonant cap
value (0.18µF vs 0.1µF) indicate this is a later revision than the schematic
documented. These differences shift the resonant frequency slightly from
what the generic schematic would predict.

## Attribution

The Panasonic Service CD schematic is Panasonic's intellectual property,
reproduced here under fair-use reference for repair purposes (the equipment
is past its commercial service life). VK3HZ's analysis text is copyright
David Smith VK3HZ. The Russian reverse-engineering source is unknown.

Cite original sources in any publication or derivative work.
