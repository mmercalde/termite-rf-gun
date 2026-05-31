# Pulse 500 Failure Mechanism — Analysis of Q701 Catastrophic Failure

**Date of failure event:** Earlier in May 2026
**Date of analysis:** 2026-05-30
**Board:** Panasonic F6645M301GP "Frankenboard"
**Outcome:** Q701 (Infineon IHW40N120R5 replacement IGBT) shorted across all
three pins. Q702 (IHW30N120R5) survived.
**Firmware running at the time of failure:** commit `63e4eb7`
(pre-VK3HZ-rewrite, with `sig2Enabled=true` auto-set on every command start)

## Executive summary

The `pulse 500` command, as implemented in the firmware version running at
the time of the Q701 failure, executed a **multi-phase sequence** that
combined three independently dangerous behaviors. Any one of them might
have been survivable; the combination was almost guaranteed to cause
catastrophic IGBT failure on a board with a marginal feedback path.

## What `pulse 500` actually did (old firmware, commit 63e4eb7)

When the user typed `pulse 500`, the firmware executed `pulseRun(500)`
which called `startRun()` followed by a 500 ms hold and then `stopRun()`.

The `startRun()` function with `softStartEn=true` (the default) did:

### Phase 1 — Soft-start warmup (2000 ms)

```c
setDuty(35);        // 35% duty cycle on GPIO4 (CN701 pin 3 command)
cmdOn();            // sets sig2Enabled=true → starts driving 110Hz on GPIO5
running=true;
delay(2000);        // hold this state for 2 full seconds
```

**Two simultaneous outputs during this 2-second phase:**

1. **GPIO4:** 222 Hz square wave at 35% duty into CN701 pin 3 (the inverter's
   command input). The ASIC interprets this as "user wants ~P40 power level".
2. **GPIO5:** 110 Hz / 50% square wave into CN701 pin 1 (the inverter's
   **own status output line**). This is the line the inverter is supposed
   to drive — we were instead actively driving it from the outside.

### Phase 2 — Linear ramp (800 ms)

```c
int steps = 800/20;  // 40 steps of 20ms each
for(int i=1; i<=40; i++){
    int d = 35 + (long)(70-35)*i/40;   // ramp 35% → 70% over 800ms
    setDuty(d);
    delay(20);
}
setDuty(70);
```

GPIO4 duty climbed: 35% → 36% → 37% → 38% → ... → 69% → 70%.
GPIO5 continued driving 110 Hz onto pin 1 throughout.

### Phase 3 — Hold at 70% for the requested pulse duration (500 ms)

```c
uint32_t deadline = millis() + 500;
while(running && deadline > millis()){
    // service serial and buttons
    delay(5);
}
```

GPIO4 held at 70% duty for 500 ms. GPIO5 still driving 110 Hz.

### Phase 4 — Auto-stop

```c
stopRun();   // calls cmdOff() which sets sig2Enabled=false
             // GPIO4 → LOW, GPIO5 → high-Z (released)
```

**Total time the inverter was being driven from the outside: 2000 + 800 + 500
= 3300 ms = 3.3 seconds.**

## The three dangerous behaviors, in detail

### Danger #1: Driving 110 Hz onto CN701 pin 1 (status line)

The status line (CN701 pin 1) is the inverter ASIC's **output**. Per the
Panasonic Service CD schematic and VK3HZ's bench measurements, the ASIC
asserts 110 Hz / 50% duty on this line only when CT701 (the input current
transformer) senses primary current flowing — i.e., when the magnetron is
warm and drawing current.

The old firmware drove 110 Hz onto this line **from the moment any command
started**, regardless of magnetron state. This created a fundamental
inconsistency:

- **Inverter's own current sense (CT701):** "No current flowing — magnetron
  isn't drawing power yet"
- **Inverter's own status output:** "Status is being asserted at 110 Hz —
  apparently the magnetron IS drawing current"

The inverter's ASIC was designed assuming these two signals would be
consistent (because under normal operation, the ASIC itself controls both).
With contradictory inputs, the closed-loop regulator's internal state
machine had no defined behavior. Different possible responses:

- Treat status as authoritative → reduce drive thinking magnetron is loaded
- Treat CT701 as authoritative → increase drive thinking no load yet
- Some weighted compromise → unpredictable middle state
- Hardware fault detection → unknown response

VK3HZ explicitly warned about this kind of scenario:

> "If the current-sense signal ever disappeared for any reason, the HV
> supply could well self-destruct as it tries to push as much power as
> possible into the load."

Our situation was even more pathological than that — we weren't just
removing feedback, we were ADDING contradictory feedback.

### Danger #2: Ramped duty cycle from 35% up to 70%

VK3HZ documents that real Panasonic ovens never reduce the PSU below
about 550 W (the P40 setting in his table). Lower power levels are achieved
by cycling the 40% setpoint on and off over a 22-second period — never by
running at sub-P40 duty continuously.

The old firmware's 35% start was below VK3HZ's P40 floor. The 70% target
was approaching the P100 maximum (75%). The 800 ms ramp passed through
the entire normal operating range and beyond — including duty values
Panasonic specifically avoids.

At each duty value during the ramp, the ASIC's internal regulator had a
different commanded power setpoint. Combined with the corrupted feedback
from Danger #1, the regulator was being asked to chase a moving target
while operating on inconsistent inputs.

### Danger #3: 500 ms hold at 70% duty with no magnetron strike

By the end of the 800 ms ramp, the inverter had been operating for 2.8
seconds. The magnetron filament had been warming all that time. By the
time the 500 ms hold phase began, the filament was likely hot enough to
start emitting electrons.

Several possible outcomes during the 500 ms hold:

**Outcome A: Magnetron struck partially.**
Filament hot, HV present. Magnetron began drawing some current. CT701
finally saw real current. But the ASIC had been fed contradictory feedback
for 2.8 seconds at that point. The regulator's response to suddenly seeing
real current (perhaps "decrease drive" because it thought it was already
loaded) could conflict with what was actually needed.

**Outcome B: Magnetron didn't strike.**
500 ms of continuous 70% duty into a no-load inverter with corrupted
feedback. The regulator pushed harder trying to find the commanded power.
Switching frequency could drift to wherever the regulator's hunting
algorithm took it. With no real feedback signal, no convergence.

**Outcome C: Magnetron struck and de-struck repeatedly.**
Intermittent strike of a marginal magnetron. CT701 reading current
occasionally, but not always. Status line being externally driven
continuously regardless. Regulator alternating between "loaded" and
"unloaded" responses. Most unstable of all scenarios.

The "magnetron firing × 10 louder" sound described by the operator is
consistent with Outcome A or C — real magnetron activity at abnormal
power levels — but could also be transformer magnetostriction screaming
under extreme drive in Outcome B.

## Why Q701 failed and Q702 survived

The asymmetric failure (Q701 short, Q702 healthy) is consistent with **the
low-side IGBT bearing the brunt of any switching event that went wrong**.
In the half-bridge topology confirmed by Michael's bench trace:

- Q702 = high-side switch (collector to +bus, emitter to switch node)
- Q701 = low-side switch (collector to switch node, emitter to DC return)

In capacitive-mode operation (switching below LC resonance) or other
abnormal switching events where the switch node fails to pre-charge during
dead time, the **low-side IGBT (Q701)** turns on with full bus voltage
across it. The high-side IGBT (Q702) doesn't suffer the same stress because
its turn-on instant is after its body diode has already conducted.

Whatever specific pathological switching pattern the confused regulator
generated, the asymmetric failure pattern points to a Q701-specific hard
turn-on event — not symmetric overcurrent in both arms.

## What the new firmware (commit `f468476` and onward) does differently

The post-rewrite firmware addresses all three dangerous behaviors:

### Fix for Danger #1 — GPIO5 is now INPUT only

```c
// New firmware setup():
pinMode(PIN_STATUS, INPUT);
attachInterrupt(PIN_STATUS, onStatusEdge, CHANGE);
```

The `sig2Enabled` variable no longer exists. GPIO5 is wired to read the
inverter's status output via a 1k+1k voltage divider, not to drive it.
The inverter's ASIC sees its own status line behave normally (asserted or
silent depending on actual magnetron current).

### Fix for Danger #2 — No ramp, no overshoot

```c
// New firmware startRun():
setDuty(warmupDuty);   // fixed 50%, no ramp
cmdOn();
// ...wait for status, then:
setDuty(curRunDuty);   // drop to 40% (commanded run duty)
```

The new firmware uses a **single fixed warmup duty** (50%) until status
appears, then drops directly to the commanded run duty (default 40%).
No ramp through intermediate values, no overshoot above run duty.

### Fix for Danger #3 — Status-gated, with ABORT timeout

```c
uint32_t deadline = millis() + statusTimeoutMs;  // default 5000ms
while(running && millis() < deadline){
    if(statusSignalLive()){ statusFound = true; break; }
    delay(5);
}
if(!statusFound){
    cmdOff();
    running = false;
    Serial.printf("[ABORT] no status signal in %lums.\n", ...);
    return false;
}
```

If the magnetron does not strike (no 110 Hz status signal appears) within
5 seconds, the firmware ABORTS automatically. No prolonged drive into a
non-firing or marginally-firing load.

### Hard duty cap at 75%

```c
else if(lc.startsWith("p")){
    int d=c.substring(1).toInt();
    if(d > 75){
        Serial.printf("[p] %d > 75 REJECTED — VK3HZ P100 = 75%% max\n", d);
    } else { ... }
}
```

Whereas the old firmware allowed up to 95% duty, the new firmware
rejects any duty above 75% (VK3HZ's measured P100 maximum).

## Same command name, completely different behavior

It is critical to understand: **`pulse 500` on the new firmware is a
fundamentally different operation than `pulse 500` was on the old firmware**.

Old `pulse 500`:
1. Drove corrupted 110 Hz feedback continuously
2. Ramped duty 35% → 70% over 800 ms
3. Held at 70% for 500 ms regardless of magnetron state
4. Total inverter drive time: 3.3 seconds, at climbing then high duty,
   with always-corrupted feedback

New `pulse 500`:
1. Reads status correctly (no driving the line)
2. Warms up at fixed 50% duty
3. Waits up to 5 seconds for magnetron strike confirmation
4. If status seen → drops to 40% run duty, holds 500 ms, stops
5. If status NOT seen → ABORTS, no fire

The new `pulse 500` is the safest first-use command precisely because the
firmware refuses to drive into an unconfirmed load condition.

## Lessons learned

1. **Never drive an opto's status output line.** It's an output. Trying to
   "help" by emulating expected status fights the device's own signaling.
2. **Don't ramp duty cycle into a controller with closed-loop regulation.**
   The controller's loop dynamics weren't designed for externally-imposed
   duty changes; let the controller's own regulator manage power level.
3. **Status-gate the drive at startup.** Inverter ASICs are designed to
   refuse status assertion when no load is present. Trust this, and use
   it as the firmware's go/no-go signal for proceeding past warmup.
4. **Default to conservative duty values.** VK3HZ's table is the bible:
   75% is the P100 maximum. Going above that exceeds Panasonic's design.

## Cross-references

- `docs/VK3HZ_FINDINGS.md` — operating principles
- `docs/PANASONIC_230V_INVERTER.md` — hardware reference
- `docs/reference/Microwave_Oven_Inverter_HV_Power_Supply.pdf` — VK3HZ canonical
- `panasonic_dual_igbt_esp32s3/panasonic_dual_igbt_esp32s3.ino` — current firmware
- Git commit `63e4eb7` — old firmware that ran during the failure
- Git commit `f468476` — VK3HZ-correct rewrite (current)
