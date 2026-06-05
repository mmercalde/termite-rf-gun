# Sources & Official Documentation — Panasonic Inverter HV PSU

Catalogue of what exists (official) vs what had to be reverse-engineered, for
driving these boards standalone. Short version: **the drive signal is RE-only.**

## Official Panasonic documentation

### "Microwave Ovens with Inverters — Technical Manual"
- Panasonic Services Company, National Training Department (Secaucus, NJ),
  prepared by Cesar Perdomo. A technician *training* manual for the whole
  inverter oven line — the closest thing to an official bench guide for these
  boards.
- Copies: ManualsLib (manual id 764833) and PDF at
  `https://media.datatail.com/docs/manual/371449_en.pdf`
  (also mirrored on avdweb, see below).
- What it covers:
  - Architecture: DB701 bridge -> DC bus -> IGBT half-bridge switched by
    20-40 kHz PWM from the DPC microprocessor; HV transformer is part of the
    oscillator resonance circuit, so drive frequency shifts with load/power.
  - HV transformer: ~2000 V secondary + ~3 V AC FILAMENT winding; D701/D702
    half-wave doubler -> ~4000 V DC. CT701 current transformer feeds output
    back to the DPC (constant-power loop).
  - Official test method: it states "it's neither necessary nor advisable to
    attempt measurement of the high voltage." Instead use an **AC ammeter**:
    1 L water beaker in the cavity, unplug the 2-pin HV connector at CN703,
    set the oven to HIGH, and read mains current to distinguish a magnetron
    fault from an inverter fault.
  - H97/H98/H99 self-diagnostic codes = "no microwave oscillation" fault
    areas (magnetron / inverter circuit). First check: magnetron filament for
    open or short-to-casing.

### Why there is NO official drive-signal spec
Panasonic treats the inverter PSU as a **non-repairable replaceable module**.
Technicians swap the whole unit; they never bench-drive it standalone. So the
220 Hz DPC->inverter command duty table was never published by Panasonic and
does not exist in any official document. Searching for it is a dead end. The
authoritative source for the command signal is the reverse-engineering work
below.

## Reverse-engineering sources (the actual drive-signal authority)

### VK3HZ (David Smith) — `docs/VK3HZ_FINDINGS.md`
- Source board: Panasonic NN-S550WF. TWO-IGBT half-bridge (Q701 GT60N90 main +
  Q702 GT30J322 flywheel) — NOT single-IGBT.
- Gives the 220 Hz command duty table per power level, the 110 Hz status
  signal behaviour, the warmup-then-drop startup, and measured HV output.
- Mains voltage of his unit is NOT stated in the doc; the 230 V assumption is
  inferred (AU callsign + ~1236 W P100). Treat his duty NUMBERS as indicative,
  the BEHAVIOUR as authoritative.

### avdweb (Albert van Dalen) — contactless HV test (KEY bench tool)
- `https://avdweb.nl/tech-tips/electronics/panasonic-hv-psu`
- Calls the board "a half-bridge converter of 1000W, 470 g" — confirms
  half-bridge / two-switch topology, and that standalone and in-oven signals
  are identical.
- SAFE go/no-go test without probing the 4 kV node: wire loop around the
  transformer core + 1N4007 diode + 10 ohm / 1 W resistor in series. If the
  inverter is producing HV:
    - **~1.6 V with the magnetron connected**
    - **~1.9 V with the magnetron disconnected**
  Near-zero while it hums = switching but NOT delivering (tank/oscillation
  fault). Real reading = HV is being made; problem is downstream (filament
  heat / strike / tube).
- Hosts a mirror of the Panasonic Technical Guide:
  `https://avdweb.nl/images/Tech-tips/Panasonic-HV-inverter/Technical-Guide-Microwave-Ovens-with-Inverters.pdf`

### tomtechtod9200 (YouTube) — F66459X91AP RE
- Reverse-engineering of an F66459X91AP (dual-IGBT sister board). Confirms the
  ~220 Hz command signal and the shared command-interface architecture.

## Filament heating (relevant to no-strike debugging)
The ~3 V filament winding is ON the HV transformer, driven by the same
oscillation — so filament heat scales with how hard the tank is driven and only
exists while the inverter is oscillating. This is the documented basis for the
warmup-then-drop startup: drive hard enough to heat the filament + sustain
oscillation, the tube strikes, back-bombardment then keeps the cathode hot, and
the drive can be reduced. (Panasonic Technical Guide + VK3HZ + avdweb all
consistent on this.)

## Chinese 家电维修 (appliance-repair) bench procedures

The factory ICT/rework spec is ODM-internal and didn't surface publicly, but the
Chinese repair-technician literature documents bench procedures for exactly this
board family and the "hums, won't strike" symptom. These are the de-facto bench
equivalent.

### Worked diagnostic case — Panasonic inverter, no oscillation
Source: `https://m.iask.sina.com.cn/b/1H1tCU8UqHJf.html`
Baseline it states: a working magnetron needs (1) filament energised, current
>10 A, and (2) >1000 V between filament and tube casing. Diagnostic order:

1. **Magnetron** — disconnect FA/F. Filament resistance very small (~0.3 ohm
   measured); each filament pin to casing must be infinite (OL).
2. **HV established, WITHOUT a 4 kV probe** — a normal meter only reads to
   ~500 V, so don't measure live. Instead: power the oven, heat ~3 s, stop,
   wait ~10 s, then measure the doubler/bus capacitor (point A) to ground. A
   healthy HV section shows a residual voltage that starts at several hundred
   volts and decays to zero (case measured ~400 V decaying). If it reads
   nothing, that section isn't delivering. (Second non-lethal HV test alongside
   the avdweb contactless loop.)
3. **Filament winding** — supplies the magnetron ~3 V; winding resistance <1 ohm.
4. **Connections** — the case's actual culprit: every individual component
   tested good, but the assembly was dead. Fault was bad contact in the
   connecting wires/connectors. Parts-good-but-dead-assembled => suspect wiring,
   not components. (Relevant to a custom launcher mount.)

### Other bench/technician sources
- "变频/定频微波炉金牌维修实训" — vocational training text, full chapters on
  magnetron substitution, filament-socket faults, HV testing, microwave-output
  measurement for INVERTER ovens (`opac.peihua.cn`, book id 01h0165797).
- "松下NN-GS575W变频微波炉常见故障速查表" — model-specific fault quick-reference
  (`gzweix.com`).
- 松下微波炉变频板维修 writeups (`shskwx.com/news/43168.html`, `/75177.html`).

## Drive signal vs feedback signal (CN701) — consolidated

**Drive / command (DPC -> inverter):**
- ~220-222 Hz square wave (measured 221.8 Hz / 4509 us). COMMON to 120 V and
  240 V boards. This is NOT the switching frequency — the inverter's own MICOM
  generates the 20-40 kHz IGBT switching internally; the 220 Hz command is only
  the power SETPOINT.
- Duty cycle = power level. Active-high (more HIGH = more power, INVERT_CMD=false).
- TTL, ~5 V peaks (meter reads 2-3 V due to duty averaging). Opto-isolated
  (IC702 + D706 + R733). ESP32 drives 3.3 V — works, but ~45% of design opto LED
  current; 74AHCT1G125 buffer gives a true 5 V if striking proves flaky.
- Duty magnitude is voltage-dependent: VK3HZ 230 V table P100 = 75% (>75%
  over-drives); the 120 V personal-oven capture was ~85%. Not portable across
  mains voltage.

**Feedback / status (inverter -> DPC):**
- 110 Hz square wave, fixed 50% duty — exactly half the command frequency,
  synchronised to it, unrelated to AC mains.
- PRESENT ONLY when current is being drawn — i.e., the magnetron is warm and
  oscillating. Absence = not struck. This is the inverter reporting "I'm running
  into a real load."
- Opto-isolated, READ-ONLY. Never drive it (driving 110 Hz onto this line
  corrupted a board previously). We tie it to GND through 1k when running
  open-loop, but reading it gives a direct electrical "did it strike" readout.
- Separate from the INTERNAL feedback: CT701 (current transformer) -> MICOM
  closes the constant-power loop inside the inverter. The 110 Hz status is the
  external summary of that to the DPC.

## Standalone-driver community (Fusor / ham RF) — open-loop drive method

These builders have driven Panasonic inverter boards open-loop for ~20 years.
Independent confirmation of the drive method (NOT Panasonic docs; treat the
per-cycle duty table as still RE-only, but the hookup/sequencing is well
established). Pin NUMBERING in these old posts is swapped vs some of our other
refs (they call command = pin 3, status = pin 1) — the ROLES/colors are the
constant, not the pin numbers.

### 4-wire standalone hookup (Fusor t=4562, t=4820)
- 2x AC mains lines.
- Control plug: pin 2 = common/ground (tie to your control-circuit ground).
- COMMAND pin: accepts an oscillatory TTL signal, duty = power, via opto.
  KEY: "the board produces FULL power if +5 VDC is applied to this pin." And
  separately: "by putting 2-3 V on the command pin the optocoupler turns on and
  power turns on — it's just on/off." So a static 5 V runs it at full; the
  duty cycle only modulates power BELOW full.
- STATUS pin: optional. To read the inverter's feedback "on" signal, pull it up
  to your +V through ~22k (22k to +12V quoted). It's the 110 Hz square wave,
  present only when the magnetron draws current.
- Control side is fully opto-isolated — can float relative to the board.

### Startup sequencing (Fusor t=4562)
"The AC mains must be hooked up for some small fraction of a second BEFORE 5 V
is applied or it won't be recognized." => power the board first, THEN assert
the command. We have not been controlling this order explicitly.

### Warmup behavior — independent confirmation (Fusor t=4820)
"The front panel sends a PWM signal to the inverter. At startup the PWM is set
at a HIGH level until the magnetron starts drawing current, i.e. the filament
has warmed up, then the inverter outputs a squarewave [110 Hz status] to tell
the front panel." => start high, hold until filament warms + tube draws current,
status appears only then. Matches VK3HZ, avdweb, and the patents.

### Drive LEVEL — the builders used a real 5 V
"Full power if +5 VDC"; "2-3 V turns the optocoupler on." 5 V = full opto drive.
Our ESP32-S3 GPIO drives 3.3 V — between "turns on" and "full 5 V." This is the
marginal-opto-current concern; the community standard is 5 V.

### Magnetron mode-hopping at partial power (Fusor t=5022)
"Find the lowest-power magnetron you can — the Panasonic one is a monster. They
mode-hop at low/medium powers, in and out of the resonance of the high-Q cavity;
a small one does this less." => "builds toward strike then drops out" at partial
drive can be mode-hopping, not a board fault.

## Patents (out-of-patent prior art confirming behavior)
- US4825028 "Magnetron with microprocessor power control": duty cycle of the
  power-control signal sets magnetron power via variable ON/OFF intervals; and
  "the microprocessor is operable to DELAY operation of the magnetron until the
  filament current has warmed up the filament." (warmup-before-strike as a claim)
- US4620078 "Power control circuit for magnetron": holds filament temperature
  above a predetermined minimum "set sufficiently high to avoid moding" while
  anode voltage is below the strike threshold in standby. (filament must be hot
  enough to strike cleanly / avoid moding)
- US4296296: duty-cycle magnetron supply; controlling signal is a square wave.
- US6936803B2 (already in repo refs): inverter current-feedback control loop.

## Panasonic service manual with published power table (right voltage class)
NN-GF668M service manual (Panasonic Shanghai), 230V 50Hz inverter oven, prints
the variable-power table (user-level average-power cycling, NOT per-cycle duty):
  HIGH 100% (22s on/0 off), MEDIUM 60%, LOW 45%, DEFROST 30% (16/6),
  SIMMER 25% (15/7), WARM 10% (8/14).
Confirms IGBTs switched by 20-40 kHz PWM from the DPC microcomputer. Public PDF
at partmaster.co.uk (nngf668mspg service manual).

## KEY OFFICIAL FINDING — magnetron filament warmup (~2 seconds)

From the NN-GF668M service manual (Panasonic, 230V), variable-power section,
verbatim: "The ON/OFF time ratio does not correspond with the percentage of
microwave power since approximately 2 seconds are required for heating of
magnetron filament."

This is Panasonic stating the warmup requirement with a NUMBER: ~2 s of filament
heating before microwave power. It ties together every other source:
- Patents US4825028 / US4620078: delay magnetron operation until filament warms;
  hold filament hot enough to strike / avoid moding.
- Fusor builders: "start the PWM high until the magnetron draws current, i.e. the
  filament has warmed up, then the status squarewave appears."
- avdweb / Panasonic Tech Guide: ~3 V filament winding on the HV transformer.

PRACTICAL CONSEQUENCE for standalone bench bring-up: brief on/off taps never
deliver the continuous ~2 s. A magnetron needs a SUSTAINED run (hold several
seconds) for the cathode to reach emission and strike. "Hums, no strike" on
short bursts is consistent with the filament never reaching temperature. The
strike develops a couple of seconds INTO a held run, not on a tap.
