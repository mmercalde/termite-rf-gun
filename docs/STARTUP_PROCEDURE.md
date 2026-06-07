# Verified Startup Procedure (AUTHORITATIVE)

Canonical, bench-proven startup for driving the Panasonic 2-IGBT inverter
(F6645M301GP) standalone from the ESP32-S3. Source of truth: firmware build
**FBLOW-RETRY-v5** in `panasonic_dual_igbt_esp32s3.ino`, developed in the
"New Panasonic boards arrival" session (2026-06-06).

This supersedes any "send a higher-duty warm-up burst then drop to the requested
level" framing, which described the OEM DPC's *low-power cycling* behaviour and
is NOT how we drive the board.

## Provenance — the cross-install experiment ("the bible")

The command *protocol* was captured from the OEM controller itself, under the
cleanest possible conditions:

- A **230V inverter** installed into a **120V microwave**.
- 230V inverter fed from its **own 230V supply**; 120V control fed from its
  **own 120V supply**.
- The **only** link between domains was **CN701**.
- The 120V control board **fired the tube** in the 230V inverter.

That proves the CN701 command protocol transfers and the 230V inverter responds
to it. Capture (`captures/oven_run1.txt`, parsed): **221.8 Hz** (period 4509 us),
asymmetric ~565 us / ~3944 us split, active-high (`INVERT_CMD = false`), no
soft-start.

**What the cross-install does and does NOT establish:** it is authoritative for
the *protocol* — 222 Hz, polarity, pin roles. It is NOT authoritative for run
*duty magnitude*: the 120V controller's command (~85% high) is calibrated for
120V operation and does NOT port to the 240V board. Duty is not voltage-portable
on a constant-power inverter (120V needs more on-time to reach a given power,
240V less). The 240V duty values come from our own working build (below), not
from the 120V capture.

## The verified startup sequence — FBLOW-RETRY-v5

Status-gated: never commits to continuous drive until the inverter confirms the
tube has struck.

1. **Startup / filament warm-up pulse.** Drive **222 Hz at 33% duty** on
   **CN701 pin 3** for a **2500 ms** attempt window.
2. **Watch feedback, CN701 pin 1**, with a **3-consecutive-reads glitch guard**
   (the RF field near the firing magnetron injects ~870k glitches/s into bare
   GPIOs; the guard rejects them):
   - **Pin 1 goes (and stays) LOW** = inverter is drawing current = **tube
     struck.** -> go to step 4.
   - **Pin 1 stays HIGH** = not struck yet (filament not hot enough). -> step 3.
3. **Retry.** Stop, wait a **1500 ms restart gap**, repeat the 2500 ms / 33%
   attempt. **Up to 8 retries.** This 2500-on / 1500-off rhythm matches the
   manual play/stop/play cadence that empirically fires cold tubes (repeated
   short drives heat the filament without holding continuous drive into an
   unstruck load).
4. **Run.** On strike, step to **75% run duty** (= VK3HZ P100 for the 240V
   board) and hold continuous. Run duty is then set by desired power.

**Filament physics:** the magnetron needs ~2 s to reach emission temperature
(Panasonic NN-GF668M: "approximately 2 seconds are required for heating of
magnetron filament"). The 2500 ms attempt window covers that; the retry loop
handles tubes that need more than one window.

**Timer handling (crash fix):** the hardware timer is started ONCE and never
re-armed. Duty changes update only `sinkTicks` live. Re-arming a running timer
(calling timerStartUs on the run transition) caused a Guru Meditation /
InstrFetchProhibited crash at PC=0x00000001 — do not reintroduce that.

**Why status-gating is the safety mechanism:** the inverter runs a constant-power
loop. Driven continuously into a load NOT drawing current (unstruck,
off-resonance, or no load), the loop concludes "need more power" and pushes its
internal switching duty up until the IGBTs fail. Not theoretical — a Frankenboard
Q701 failed within seconds at **30% duty in force mode** (open-loop, status
ignored), fuses intact, fault faster than fuse response
(`docs/failure_analysis/`). Watching pin 1 for strike before going continuous is
exactly what prevents that.

## Polarity / pin reference (confirmed)
- **CN701 pin 3** = command IN (we drive). 222 Hz, duty = power, active-high.
- **CN701 pin 1** = status/feedback OUT (we READ, never drive). Open-collector
  from the inverter, **pulled HIGH by the DPC's pull-up**; the inverter pulls it
  **LOW at 110 Hz when the magnetron draws current**. **LOW = struck.** With the
  DPC removed the line sits at 0V — so in our standalone rig we supply the pull-up
  and read the line. Driving this line previously corrupted a board: read-only.
- **CN701 pin 2** = 0V common.

## Duty values — reconciled
- **Startup pulse: 33%** @ 222 Hz (the "~38%" mentioned at the bench was a
  near-miss for 33%).
- **Run: 75%** @ 222 Hz on strike (VK3HZ P100 for 240V; cap 75%, >75% over-drives).
- **~85% is the 120V capture only** — NOT used on the 240V board (not portable).

## Document history
- 2026-06-07 Created from FBLOW-RETRY-v5 (session "New Panasonic boards arrival",
  2026-06-06). Documents the cross-install provenance, the 33%-pulse /
  watch-pin1-LOW / 8x-retry / 75%-run status-gated sequence, the 3-read glitch
  guard, the single-arm timer crash fix, and the pin-1-LOW = struck polarity.
  CORRECTS an earlier draft that wrongly claimed the cross-install capture
  refuted the 75% run-duty cap: the cross-install establishes the protocol, not
  the 240V run duty. 75% run is the proven value; 85% is the 120V capture and
  does not port. Also supersedes the "warm-up burst then drop" framing.
