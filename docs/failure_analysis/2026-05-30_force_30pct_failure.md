# Frankenboard Failure #2 — Q701 Killed at 30% Force Mode

**Date:** 2026-05-30 (evening, same day as failure #1 analysis)
**Board:** Panasonic F6645M301GP "Frankenboard"
**Outcome:** Q701 (Toshiba GT50J327, freshly installed) shorted. Q702
(Infineon IHW30N120R5, new unit) tests healthy. Both 2A fuses intact.

## Context

This is the **second** Q701 catastrophic failure on this specific
Frankenboard. The first failure (`pulse 500` on old firmware, May 2026)
was attributed to a specific firmware bug (driving 110 Hz onto the status
output line, corrupting the ASIC's feedback loop) plus an aggressive
soft-start ramp. The new firmware (commit `f468476`) was rewritten to fix
both issues.

Going into today's test:
- New firmware confirmed running (banner shows VK3HZ MODE, no `sig2Enabled`)
- ESP32 pin assignments verified, voltage divider working
- GPIO5 reads correctly (110 Hz status detection confirmed via DS213 sig gen)
- 240V split-phase mains applied through 2 A fast-blow fuses (one per leg)
- Both IGBTs soldered into board:
  - Q701 = original Toshiba GT50J327 (the part Panasonic originally specced)
  - Q702 = surviving IHW30N120R5 (from failure #1 event)
- Magnetron connected with verified FA-to-HV / A-to-heater / body-to-chassis
- Zero-cross detection working (60 Hz clean after cold solder repair)
- R702 warming normally at idle (bias supply working)
- `statusinfo` showed 0 edges at idle (no spurious signals)

## The test sequence

Goal: replicate the previous-session 120 V single-IGBT bring-up approach
(steady fixed duty, no warmup state machine, no status corruption) at a
conservative 30% duty as a first attempt.

Commands sent to ESP32:

```
p 30          → set run duty to 30%, response: [p] run duty=30%
force         → toggle force mode, response: [force] ENABLED — DANGEROUS
on            → start drive, response: [ON] FORCE mode: open-loop, no status check
```

Expected behavior (based on single-IGBT bench experience):
- 222 Hz / 30% duty drive from cycle one
- HV present, filament heating
- Insufficient duty to strike magnetron
- Audible hum, no oscillation
- Bounded under-drive failure mode — no IGBT damage

## What actually happened

Within seconds of typing `on`:

- **Slight "snap" or "thunk" sound** from the inverter
- Less audible than the original failure event (which was at 70% duty
  for 500 ms with corrupted feedback)
- No smoke
- No oscillation
- **No fuse blow** (both 2 A fuses tested intact afterward)

User typed `off` and the rest of the session was verification only.

Post-mortem with multimeter (mains disconnected, bus cap discharged):

| Component | Reading | Status |
|---|---|---|
| Q701 (Toshiba GT50J327) | C-E shorted in both directions | **DEAD** |
| Q702 (IHW30N120R5) | C-E reverse 0.4V, all other paths OL | Healthy |
| Both 2 A fuses | Continuity | Intact |

## Why this changes the analysis

This failure was at conditions that should have been benign:

- **Force mode at 30% duty** is well below VK3HZ's P40 floor (33%) and far
  below the previous-session single-IGBT working point (87.5%)
- **No status corruption** — new firmware reads GPIO5, never drives it
- **No duty ramp** — fixed value from cycle one
- **No sustained high-duty hold** — failed within seconds of starting
- **Brief drive time** — much less than the 3.3 seconds the original
  `pulse 500` was running

The new firmware specifically eliminated the three independently
dangerous behaviors identified in the previous post-mortem:
1. ✓ No 110 Hz drive on pin 1 (status output line)
2. ✓ No ramp through wide duty range
3. ✓ No sustained high duty in unknown load state

And yet Q701 still died.

## Asymmetric failure pattern across all three Frankenboard events

| Failure | Q701 outcome | Q702 outcome | Drive conditions |
|---------|--------------|--------------|------------------|
| Original donor damage | (unknown what failed first) | (both eventually dead) | Pre-project history |
| May 2026 (pulse 500) | Catastrophic short | Survived | Old firmware, 70% duty, 500 ms, status corruption |
| 2026-05-30 (force 30%) | Catastrophic short | Survived | New firmware, 30% duty, seconds, no corruption |

**Q702 has survived every failure event.** Q701 dies. Three events, same
asymmetric pattern. This is not random IGBT weakness — it's something
specific to the Q701 (low-side) position on this board.

## What this rules out

1. **"The firmware fix is sufficient"** — Not on this board. The new
   firmware did exactly what it should: steady-duty drive, no status
   manipulation, brief duration. Q701 died anyway.

2. **"IGBT replacement quality"** — The Toshiba GT50J327 is the original
   Panasonic-specified part for this exact position. Not an Infineon
   substitute. Not a Chinese clone. Original spec, fresh part. Dead in
   seconds at 30% duty.

3. **"Drive duty was too high"** — 30% is below the lowest continuous
   operating point Panasonic uses. If 30% kills an IGBT on this board,
   no duty value would have been safe.

## What this points to

The Frankenboard has accumulated damage in components OTHER than the IGBTs.
Most likely candidates:

1. **Q701-specific gate driver** (Q703/Q704/Q705 totem-pole) — if any of
   these have damage, the gate drive waveform on Q701 specifically could
   be wrong (slow turn-off, missed dead time, etc.) leading to hard
   switching events. Q702's gate driver is independent and could be fine.

2. **R702 bias network or related components** — if the +20 V rail has
   ripple, sag, or instability on the low-side, Q701 sees poor gate
   drive while Q702 might still be adequately driven through its
   different gate driver path.

3. **Damaged snubber or resonant tank components** — C701 measured as
   0.18 µF (different from Panasonic schematic 0.1 µF — but this is
   "expected" board-revision difference per the Panasonic Service CD).
   If C701 has internal damage (microcrack, partial dielectric breakdown),
   the resonant LC tank operates at unintended frequency, putting
   switching events into the destructive sub-resonant region.

4. **T701 primary winding** — partial shorted turns from previous events
   would change the effective inductance, shifting f_r. Could put
   switching into capacitive mode regardless of duty value.

5. **DB701 bridge rectifier** — partial diode damage from the original
   donor failure could cause bus voltage instability, ringing, or
   asymmetric bus charging that the low-side IGBT bears the brunt of.

We cannot diagnose which of these is the actual cause without component-
level investigation that's not practical for a board with this much
contamination history. **The Frankenboard is now permanently retired.**

## What this means for the new factory boards

The new factory boards (in transit from China at time of writing) remain
the path forward. Critical questions they will answer:

**Will a factory board behave like:**
- The single-IGBT board (bounded under-drive — hum at 30%, no damage)?
- The Frankenboard (catastrophic Q701 failure at 30%)?

If factory boards behave like the single-IGBT board, the issue was
purely Frankenboard accumulated damage, and the firmware fix combined
with conservative duty testing is sufficient.

If factory boards ALSO behave like the Frankenboard, then the dual-IGBT
topology has a fundamentally different failure envelope than the single-
IGBT topology — possibly because the half-bridge has hard-switching
failure modes that don't exist in the single-IGBT quasi-resonant
topology.

We genuinely do not know which case applies. The new boards will tell us.

## Frankenboard status

**RETIRED.** No more bring-up attempts. Stays on shelf as:
- Reference for component photos (R702 markings, C701/C704/C705 markings, etc.)
- Source of cross-reference data for the new boards
- Historical record

Q702 (the surviving IHW30N120R5) may be reused on a factory board.
Q701 position is now empty.

## Components used / lost

| Event | Q701 part | Outcome | Cost |
|-------|-----------|---------|------|
| May 2026 | IHW40N120R5 (Infineon) | Killed | ~$8 |
| 2026-05-30 | Toshiba GT50J327 (original) | Killed | ~$15 |

Remaining IGBT inventory:
- IHW40N120R5: 3 unused
- IHW30N120R5: 3 unused (plus the surviving one from the failures)

## Procedural lessons

1. **"Force mode at low duty is safe" was an unverified assumption.** It
   was based on single-IGBT bench experience, extrapolated to the dual-
   IGBT topology without independent verification. The first time we
   ran it on the dual-IGBT board, an IGBT died.

2. **Bounded failure modes are board-specific, not topology-specific.**
   The single-IGBT board's tolerance for under-drive doesn't generalize.

3. **Contaminated boards should be diagnosed extensively or retired.**
   The Frankenboard had a documented history of prior abuse. We could
   not isolate the residual damage with the diagnostics available. We
   should have retired it earlier rather than sacrificing another IGBT.

4. **2 A fuses are too slow to save IGBTs from microsecond switching
   failures.** They protect against fire and sustained fault current,
   not against IGBT thermal runaway. The fuses-intact outcome is
   consistent with the failure being faster than the fuses could react.

## Cross-references

- `docs/failure_analysis/2026-05-30_pulse500_failure_mechanism.md` — first failure analysis
- `docs/bench_observations/2026-05-30_frankenboard_dry_run.md` — dry-run with no IGBTs
- `docs/VK3HZ_FINDINGS.md` — operating principles
- Git commit `f468476` — VK3HZ-correct firmware that ran during this failure
