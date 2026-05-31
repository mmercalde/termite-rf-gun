# Factory Board Bring-up Procedure

Procedure for first-fire testing of new factory F6645M301GP boards, derived
from the lessons of two Frankenboard Q701 failures (see
`docs/failure_analysis/`).

**Boards in transit at time of writing:** 3× F6645M301GP factory units from
AliExpress, "100% test working" — arriving 2026-06-01 to 2026-06-07.

## Pre-test inventory required

- ESP32-S3 with new firmware (commit `f468476` or later)
  flashed and running
- Voltage divider on GPIO5 confirmed working (1 kΩ series + 1 kΩ pulldown,
  scope-verified ~2.5 V swing on a 5 V status signal)
- Two 2 A fast-blow fuses (one per hot leg, split-phase 240 V)
- Both scopes battery-powered (DS213 + UT81B), USB unplugged during test
- Multimeter
- Magnetron with verified wiring (FA = HV, A = heater return, body = chassis)
- IGBT pair ready for install: 1× IHW40N120R5 (Q701) + 1× IHW30N120R5 (Q702)
  - Or original Toshiba GT50J327 + GT35J321 if obtainable

## Why the procedure matters

The Frankenboard demonstrated that the dual-IGBT topology has a destructive
failure mode at low duty cycle in force mode. Specifically: Q701 failed
within seconds at 30% duty force mode, fuses intact (fault faster than
fuse response), no smoke.

Open question this procedure answers:

- **Was the Frankenboard failure board-specific contamination** (in which
  case factory boards will tolerate the same drive conditions without
  failure)?
- **Or is it a property of the dual-IGBT topology** (in which case factory
  boards will fail the same way)?

The procedure is designed to find out without sacrificing IGBTs.

## Stage 0 — Board incoming inspection (no power)

For each factory board:

### Component verification

Compare against `docs/reference/Panasonic_ServiceCD_Schematic.png`:

- [ ] R702 reads 3.5 kΩ (or 15 kΩ per schematic — note which revision)
- [ ] R701 reads OL (HV bleeder, normal — value too high for meter)
- [ ] DB701 bridge rectifier: 4 diodes, all healthy F/R readings
- [ ] C702 (4 µF bus cap): not shorted, brief charge spike on diode mode
- [ ] L701 mains choke: low DC resistance, no opens
- [ ] CT701 current transformer: low resistance both windings
- [ ] CN701 connector: pins 1, 2, 3 traceable to opto LEDs/phototransistors

### Component value photos

Photograph and add to `docs/bench_observations/` with date and board serial:

- C701 marking (resonant cap value — confirm 0.1 µF or 0.18 µF)
- C704, C705 markings (doubler caps — 8.2 nF or asymmetric?)
- R702 marking (3.5 kΩ or 15 kΩ?)
- Any other obvious revision-marker components

### Bench trace half-bridge connectivity

Multimeter continuity (no power):

- [ ] Q702 emitter pad → Q701 collector pad → switch node (should be one net)
- [ ] Q701 emitter pad → Cap702 → DC return
- [ ] Q702 collector pad → +DC bus (downstream of DB701/L701)

Match to Panasonic Service CD schematic. Any deviation = board different
from documented design, investigate before powering.

## Stage 1 — Mains-only sanity test (NO IGBTs installed)

IGBTs out of the board. Bias supply verification + ASIC alive check.

### Setup

- [ ] IGBTs NOT in board (kept on heatsink with thermal paste but not soldered in)
- [ ] CN702 wired to 240 V split-phase via 2× 2 A fuses (one per leg)
- [ ] CN701 cable wired ESP32 ↔ board (yellow GPIO4 / brown GND / orange GPIO5)
- [ ] Magnetron NOT connected yet (HV output disconnected/insulated)
- [ ] DS213 probe on Q701 gate pad, ground on Q701 emitter pad
- [ ] UT81B probe on CN701 pin 1 (orange), ground on CN701 pin 2 (brown)
- [ ] ESP32 power-cycled, banner shows VK3HZ MODE at boot
- [ ] `statusinfo` reports 0 edges
- [ ] `zc` reports 60 Hz (confirms mains-side opto working)

### Test

Apply 240 V mains. Watch for fuse blow or smoke. If either, kill power.

Wait 5 seconds for ASIC bias to come up. Then:

- [ ] R702 warming normally (warm to touch, not too hot to touch within 30s)
- [ ] No smoke, no smell, no audible fault
- [ ] `statusinfo` still 0 edges (no spurious 110 Hz from ASIC)
- [ ] CN701 pin 1 sits at some defined level (HIGH ~5 V is normal)

Now send command in force mode at low duty:

```
p 30
force
on
```

Watch DS213 for 5-10 seconds. Then `off`. Capture:

- [ ] Q701 gate Vpp (expected 26-28 V if bias rail is good)
- [ ] Q701 gate frequency range (note minimum and maximum observed)
- [ ] CN701 pin 1 state during drive (should stay quiet, since no current)

**Critical comparison:** does this factory board hunt the same 21-33 kHz
range as the Frankenboard, or settle differently?

Repeat at:
```
off
p 40
on
```
Capture same data. Then 50%, 60%, 75% — each separated by `off`.

### Pass criteria for Stage 1

- All duty values produced visible gate drive
- No smoke, no smell, no fuse blow
- R702 stayed warm-but-stable (not climbing toward hot)
- ASIC didn't latch into a state requiring power cycle to clear

### Compare to Frankenboard

If hunting range is identical to Frankenboard (21-33 kHz at 40%): this is
inherent to the topology, not board-specific. Stage 2 carries the same
IGBT risk as the Frankenboard did.

If hunting range is different (e.g., 28-40 kHz, never below f_r): this is
board-specific. Stage 2 should be safe at the same drive conditions that
killed the Frankenboard.

**Document both results regardless of outcome.**

## Stage 2 — IGBTs installed, magnetron load attached

Only if Stage 1 passed and you accept the residual IGBT risk.

### Setup

- [ ] Power off, bus cap discharged, verified with meter at C702
- [ ] Install Q701 (IHW40N120R5 fresh) and Q702 (IHW30N120R5 fresh
  or surviving) — solder both
- [ ] Magnetron HV connection to FA pin (confirmed earlier)
- [ ] Magnetron heater AC return to A pin
- [ ] Magnetron body grounded via mounting flange to chassis
- [ ] Continuity check: chassis ground to magnetron body = near 0 Ω
- [ ] Continuity check: F to FA = filament resistance (typically < 1 Ω)
- [ ] Continuity check: F or FA to magnetron body = OL (vacuum gap intact)
- [ ] Replace any blown fuses
- [ ] Both scopes ready and battery-powered

### First test — direct replication of single-IGBT bring-up approach

```
p 30
force
on
```

Listen carefully for 3-5 seconds. Then `off`.

**Pass criteria for first test:**

- [ ] Faint hum audible (filament heating + idle inverter)
- [ ] No "snap" or "thunk" sound
- [ ] No smoke
- [ ] No fuse blow
- [ ] Q701 still tests healthy via diode mode after `off`

If any of these fail, the factory board has the same problem as the
Frankenboard. STOP. Do not install IGBTs in other factory boards. Re-
evaluate whether the dual-IGBT topology is viable for our application.

If all pass, the factory board is fundamentally safe and we proceed.

### Step up duty

```
off
p 40
on
```

Listen for striking sound. Then `off` after 3-5 seconds.

```
off
p 50
on
```

```
off
p 60
on
```

```
off
p 75
on
```

At each step:
- Listen for magnetron striking (unmistakable buzz — different from hum)
- If striking: 2 A fuses will blow within seconds, STOP (this is success)
- If only hum: continue to next duty value
- If snap/thunk: STOP, Q701 likely lost, diagnose

### If 75% doesn't strike

Either:
- The firmware cap needs raising (try 87% to match captured signal)
- The magnetron has a problem
- The board has a problem
- We're missing something fundamental

Stop, analyze, don't push further without understanding why.

## Stage 3 — Sustained operation

Only after Stage 2 produces magnetron striking at some duty.

- Replace 2 A fuses with appropriately-sized production fuses (6-8 A
  slow-blow for normal magnetron operation at ~5 A draw)
- Repeat the working duty with longer pulse durations
- Verify status signal (110 Hz on CN701 pin 1) appears during oscillation
- Build statistics: does it strike reliably on first attempt every time?
- Note magnetron temperature, RF output if measurable, mains current draw

## Lessons embedded in this procedure

1. **Stage 1 before Stage 2** — characterize gate drive at multiple duty
   values without IGBTs in circuit. The Frankenboard's hunting range was
   a diagnostic we should have run before installing IGBTs, not after
   failure.

2. **Conservative duty first** — start at 30% even though we know 30%
   isn't enough to strike. Bounded under-drive is the safe regime to
   confirm bounded operation before pushing further.

3. **Force mode for direct waveform replication** — matches the captured
   single-IGBT working signal shape. No firmware-side state machine
   complexity during the test. Same signal pattern that worked before.

4. **One change per test** — duty value steps in isolation. Don't change
   firmware AND duty AND wiring simultaneously. When something fails, the
   change that triggered it should be obvious.

5. **Stop on snap/thunk/smoke/smell** — these are unambiguous failure
   signals. Don't try to "push through" them.

6. **Document everything** — bench observations folder gets photos and
   notes from each test. Future investigations should have data to work
   from.

## Cross-references

- `docs/failure_analysis/2026-05-30_pulse500_failure_mechanism.md`
- `docs/failure_analysis/2026-05-30_force_30pct_failure.md`
- `docs/bench_observations/2026-05-30_frankenboard_dry_run.md`
- `docs/VK3HZ_FINDINGS.md`
- `docs/PANASONIC_230V_INVERTER.md`
- `docs/reference/Panasonic_ServiceCD_Schematic.png`
- `panasonic_dual_igbt_esp32s3/panasonic_dual_igbt_esp32s3.ino` — firmware
