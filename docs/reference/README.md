# Reference Documents

This folder contains archived copies of key reference documents for the
Panasonic inverter project. They are stored locally so the project does
not depend on external sites staying online.

## Files

### `Panasonic_Inverter_Schematic_Annotated.pdf`

Reverse-engineered schematic of a Panasonic universal-input (110V/220V)
microwave inverter board, with Russian and English functional-block
annotations. Source unknown (possibly a Russian electronics repair forum).
Captures the **dual-IGBT half-bridge topology** that matches our
F6645M301GP family.

**Key components labeled:**
- Q701 = G60N321 — bottom-side power IGBT (low-side switch)
- Q702 = GT30J322 — top-side power IGBT (high-side switch)
- Q703, Q705 = C2785 — NPN gate driver totem-pole transistors
- Q704 = A1174 — PNP gate driver totem-pole transistor
- C701 = 0.68µF / 500V — **resonant tank capacitor** (critical part)
- C702 = 4µF / 250V — DC bus filter (deliberately small, see VK3HZ)
- C704, C705 = 8200pF / 3kV — HV doubler caps
- L701 — mains choke
- CT701 — input current transformer (constant-power regulation sense)
- D704, D705 = 6.2V (A6V71) — power supply low-voltage detect zeners
- R715-717 = 4.5kΩ 15W — sand-bar wirewound (the bias-supply resistor that runs hot)
- IC701, IC702 — opto-isolators at CN701 (command in / status out)
- D706 — status LED feedback driving IC701

**Controller IC functional blocks** (bottom of schematic, dashed boundary):
1. Power supply circuit low-voltage detect (pins 2, 3, 5, 6)
2. Power control (pins 7, 8)
3. Switching control (pins 9, 15, 13, 4)
4. Power control (additional)
5. Feedback signal circuit
6. Start control (pins 10, 11, 12)
7. CN701 interface (pins 14, 13, 1, 4)

Note: this controller's pin numbering differs from the FCC ACLAP4T01
schematic's IC801. They appear to be different generations of the same
analog-ASIC family Panasonic used across this product line.

### `Microwave_Oven_Inverter_HV_Power_Supply.pdf` (to be added)

The canonical VK3HZ reverse-engineering writeup by David Smith VK3HZ
(vk3hz [at] wia.org.au). Original URL:
http://www.vk3hz.net/amps/Microwave_Oven_Inverter_HV_Power_Supply.pdf

Mirrored docplayer URL:
https://docplayer.net/21544207-Panasonic-microwave-oven-inverter-hv-power-supply.html

This file should be copied into this folder manually (it's not committed
yet — see the project README for instructions). Once added, see
`../VK3HZ_FINDINGS.md` for a structured summary of its content.

**Attribution:** All rights to the VK3HZ document belong to David Smith.
This is an archived reference copy for project continuity. The original
should be cited in any publication or derivative work.

---

## How to add the VK3HZ PDF (manual step)

```bash
cp ~/Downloads/Microwave_Oven_Inverter_HV_Power_Supply.pdf \
   ~/termite-rf-gun/docs/reference/
cd ~/termite-rf-gun
git add docs/reference/Microwave_Oven_Inverter_HV_Power_Supply.pdf
git commit -m "Add archived copy of VK3HZ canonical inverter PDF"
git push
```

The file is ~466 KB. Both reference PDFs together are under 1 MB, well
within reasonable GitHub repo size.
