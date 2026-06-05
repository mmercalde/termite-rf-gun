/* ============================================================================
 *  panasonic_dual_igbt_esp32s3.ino  -  Panasonic 2-IGBT inverter driver
 *  ESP32-S3 Dev Module · core 2.x/3.x · 4MB · PSRAM off · USB CDC On Boot
 * ----------------------------------------------------------------------------
 *  TARGET BOARD: Panasonic F6645M301GP / "M3GP" 2-IGBT inverter (240V)
 *  ORIGINAL IGBTs (obsolete): Toshiba GT50J327 big + GT35J321 small
 *  REPLACEMENT IGBTs:         Infineon IHW40N120R5 big + IHW30N120R5 small
 *                             (TRENCHSTOP RC-H5, 1200V, monolithic body diode)
 *
 *  ======================================================================
 *  MAJOR REWRITE 2026-05-30 — incorporates VK3HZ canonical findings:
 *
 *  1) PIN 3 (orange) is the STATUS INPUT from the inverter, NOT a feedback
 *     pin we drive. The inverter emits a 110Hz/50% square wave on this line
 *     when the magnetron is warm and drawing current. We READ it, not write.
 *
 *  2) STARTUP — follows VK3HZ_FINDINGS.md (230V board). Drive the run duty
 *     (default 75% HIGH @ 222Hz = VK3HZ P100) from cycle one, no soft-start;
 *     the inverter's own current loop handles inrush. Watch PIN_STATUS for the
 *     110Hz signal, declare RUN once seen, ABORT if it never appears.
 *
 *  3) RUN DUTY = 75%, CAP 75% (VK3HZ: P100 = 75%, >75% over-drives). NOTE the
 *     ~85% figure from our 120V personal-oven capture does NOT apply here:
 *     duty isn't voltage-portable on a constant-power inverter (120V needs more
 *     on-time, 240V less). 222Hz/polarity/no-soft-start transfer; duty doesn't.
 *
 *  4) NO MORE sig2 OUTPUT. The previous firmware drove GPIO5 with a 110Hz
 *     emulation pattern, which fought the inverter's own status signal on
 *     the same line. Now GPIO5 is INPUT only.
 *
 *  HARDWARE CHANGE REQUIRED for safe status reading:
 *  --------------------------------------------------
 *  The board pulls CN701 pin 3 up to 5V via its internal pull-up. With only
 *  a 1k series resistor (existing), GPIO5 would see ~5V on the high state,
 *  exceeding ESP32-S3's 3.3V max input.
 *
 *  Add a 2.2k pulldown from GPIO5 to GND to form a voltage divider:
 *
 *       5V (board pull-up via ~1-2k internal)
 *         |
 *      pin3 -------- 1k ------- GPIO5 ----+
 *                                         |
 *                                        2.2k
 *                                         |
 *                                        GND
 *
 *  GPIO5 voltage when board high:  ~3.0V  (safe)
 *  GPIO5 voltage when inverter sinks pin3 low: ~0V (safe)
 *
 *  If you DO NOT add the 2.2k pulldown, the status read may be unreliable
 *  and could damage the GPIO over time. The firmware still runs — abort
 *  detection still works on timeout — but it won't see the actual status
 *  signal correctly until the divider is in place.
 *
 *  ======================================================================
 *
 *  WIRING (updated):
 *    GPIO4 = PWM command out (push-pull TTL 3.3V) -> CN701 pin 1 (YELLOW)
 *    GPIO5 = STATUS INPUT from inverter (via 1k + 2.2k divider, see above)
 *              -> CN701 pin 3 (ORANGE)
 *    GND                                          -> CN701 pin 2 (BROWN)
 *    GPIO7 = zero-cross in (monitor only)
 *    GPIO6 = capture tap (divider) for 'log' mode
 *    GPIO1 = Button1 ON/OFF ; GPIO2 = Button2 power step
 *
 *  CONSOLE (115200):
 *    on / off          start (drive run duty, status-gated) / stop
 *    pulse <ms>        timed burst (1..30000ms at run duty, auto-stop). 'off' aborts.
 *    p <10..75>        set RUN duty % (default 75 = VK3HZ P100; cap 75)
 *    f <hz>            set command frequency (default 222)
 *    tmo <ms>          set status-timeout ms (default 5000)
 *    force             ignore status — open-loop run (proven single-IGBT path)
 *    nostatus          skip status detection, treat strike as complete
 *    statusinfo        print live status-signal edge rate
 *    log / logdump     capture mode on GPIO6
 *    zc / status / ?
 * ==========================================================================*/

#include "soc/gpio_struct.h"
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

// ---- WiFi AP (self-hosted, no router — for off-grid bench use) ----
// Join SSID 'TermiteRF' (pass 'magnetron'), browse to http://192.168.4.1/
// The PWM runs on a hardware-timer ISR, INDEPENDENT of WiFi — so a dropped
// web packet (e.g. RF hiccup at strike) does NOT drop the command to the
// inverter. The web link only sets parameters and start/stop.
const char* AP_SSID = "TermiteRF";
const char* AP_PASS = "magnetron";       // must be >= 8 chars
WebServer   server(80);

// ---- web / remote-control state ----
bool     webControlled    = false;       // current run was started from the web UI
uint32_t lastWebContactMs = 0;           // updated each /state poll (keep-alive)
bool     deadmanOn        = false;        // auto-off if web contact lost (OFF by default
                                          // so a WiFi hiccup at strike can't abort a run)
uint32_t deadmanSec       = 15;           // grace period before deadman auto-off
uint32_t maxRunSec        = 60;           // HARD cap: web runs auto-off after this
uint32_t runStartMs       = 0;            // when the current run began
volatile uint32_t statusHzX10 = 0;        // measured status-line edge rate x10 (feedback)

constexpr int PIN_PWM     = 4;   // command out -> CN701 pin1 (YELLOW)
constexpr int PIN_STATUS  = 5;   // status IN  <- CN701 pin3 (ORANGE), via divider
constexpr int PIN_ZC      = 7;   // zero-cross monitor
constexpr int PIN_BTN_ONOFF = 1;
constexpr int PIN_BTN_POWER = 2;
constexpr int PIN_LOG     = 6;   // capture tap

constexpr bool INVERT_CMD = false;

constexpr uint32_t TICKS = 200;        // 0.5% duty resolution
uint32_t cmdFreqHz = 222;              // 222Hz command — COMMON to 120V and 240V boards
int      curDuty   = 75;
bool     running   = false;

// ---- VERIFIED startup parameters (per VK3HZ_FINDINGS.md) ----
// VK3HZ characterized a Panasonic NN-S550WF — a 230V (Australian) board, so
// his duty table is the VOLTAGE-MATCHED reference for our 240V F6645 boards.
//
// What transfers from our own 120V bench capture (captures/oven_run1.txt) is
// ARCHITECTURE ONLY, not duty magnitude:
//   * 222 Hz command period (4509us)         -> COMMON to both voltages
//   * active-high polarity, more HIGH = power -> INVERT_CMD = false
//   * NO soft-start ramp; DPC commands the run duty from cycle one and the
//     inverter's own current loop handles magnetron inrush
// Duty does NOT transfer: constant-power means a 120V board needs more on-time
// (~85%) to reach full power, a 240V board reaches it at LESS on-time. Using
// the 120V 85% on a 240V board would be genuine over-drive.
//
// So duty magnitudes come from VK3HZ's 230V table:
//   P40=33  P50=33  P60=40  P70=55  P80=62  P90=69  P100=75   (% HIGH duty)
//   "P100 = 75%. Anything above 75% is over-driving the inverter."
//
// Startup (kept): drive the run duty from cycle one, watch the 110Hz status
// line, declare RUN only once the magnetron is oscillating, ABORT if status
// never appears (no-load protection for the constant-power loop). For P50+
// (our regime) VK3HZ shows continuous drive at the run value — no separate
// warmup duty. We never drive the status line.
uint32_t statusTimeoutMs  = 5000;      // 5s to see status before aborting

constexpr int DUTY_MIN   = 10;
constexpr int DUTY_MAX   = 75;         // VK3HZ: P100 = 75%, >75% is over-driving

// RUN power steps (% HIGH duty) = VK3HZ continuous P-levels P40..P100.
const int RUN_STEPS[] = {33, 40, 55, 62, 69, 75};
const int RUN_COUNT = sizeof(RUN_STEPS)/sizeof(RUN_STEPS[0]);
int runIdx = 5;                        // DEFAULT 75% == VK3HZ P100
int curRunDuty = 75;

const uint32_t FREQ_LIST[] = {110, 150, 165, 180, 200, 222, 233, 250, 280, 300};
const int FREQ_COUNT = sizeof(FREQ_LIST)/sizeof(FREQ_LIST[0]);
int freqIdx = 5;                       // 222Hz

// Override flags
bool forceMode    = false;             // 'force' = ignore status, open-loop run
bool statusBypass = false;             // 'nostatus' = skip status detection

// ---- ISR PWM ----
hw_timer_t* pwmTimer = nullptr;
volatile uint32_t tickCounter = 0, sinkTicks = 0;
volatile uint32_t periodTicks = TICKS;   // ticks per command period (200 normally;
                                         // 167 in captured-profile playback)
volatile bool drvEnabled = false;

void IRAM_ATTR onPwmTick() {
    if (!drvEnabled) { GPIO.out_w1tc = (1u<<PIN_PWM); return; }
    tickCounter++;
    if (tickCounter >= periodTicks) tickCounter = 0;
    bool on = (tickCounter < sinkTicks);
    if (INVERT_CMD) on = !on;
    if (on) GPIO.out_w1ts = (1u<<PIN_PWM);
    else    GPIO.out_w1tc = (1u<<PIN_PWM);
}

// Start the timer with an EXPLICIT per-tick microsecond value (no freq truncation).
// Used by captured-profile playback so the period is exact (167 ticks x 27us = 4509us).
void timerStartUs(uint32_t us) {
    if(us<1) us=1;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    if(!pwmTimer){pwmTimer=timerBegin(1000000);timerAttachInterrupt(pwmTimer,&onPwmTick);}
    timerStop(pwmTimer);timerWrite(pwmTimer,0);
    timerAlarm(pwmTimer,us,true,0);timerStart(pwmTimer);
#else
    if(!pwmTimer){pwmTimer=timerBegin(0,80,true);timerAttachInterrupt(pwmTimer,&onPwmTick,true);}
    timerAlarmWrite(pwmTimer,us,true);timerAlarmEnable(pwmTimer);
#endif
}

void timerStart(uint32_t fhz) {
    uint32_t f=fhz; if(f<1)f=1; if(f>5000)f=5000;
    uint32_t us=1000000UL/(f*TICKS); if(us<5)us=5;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    if(!pwmTimer){pwmTimer=timerBegin(1000000);timerAttachInterrupt(pwmTimer,&onPwmTick);}
    timerStop(pwmTimer);timerWrite(pwmTimer,0);
    timerAlarm(pwmTimer,us,true,0);timerStart(pwmTimer);
#else
    if(!pwmTimer){pwmTimer=timerBegin(0,80,true);timerAttachInterrupt(pwmTimer,&onPwmTick,true);}
    timerAlarmWrite(pwmTimer,us,true);timerAlarmEnable(pwmTimer);
#endif
}

// ---- zero-cross capture (monitor only) ----
volatile uint32_t zcLastUs = 0, zcPeriodUs = 0;
volatile uint32_t zcAccum = 0;
volatile uint8_t  zcEdgeCount = 0;
volatile bool zcSeen = false;
constexpr uint32_t ZC_LOCKOUT_US = 4000;
void IRAM_ATTR onZeroCross() {
    uint32_t now = micros();
    uint32_t dt = now - zcLastUs;
    if (zcLastUs && dt < ZC_LOCKOUT_US) return;
    if (zcLastUs) {
        zcAccum += dt;
        zcEdgeCount++;
        if (zcEdgeCount >= 2) {
            zcPeriodUs = zcAccum;
            zcAccum = 0;
            zcEdgeCount = 0;
        }
    }
    zcLastUs = now; zcSeen = true;
}

// ---- STATUS signal detection (110Hz square wave from inverter) ----
// Count edges on PIN_STATUS. At 110Hz/50% we expect 220 edges/second.
// Over a 200ms window: 44 expected. If we see >= STATUS_EDGES_MIN edges,
// signal is present (magnetron warm and oscillating).
volatile uint32_t statusEdgeCount = 0;
constexpr uint32_t STATUS_WINDOW_MS = 200;
constexpr uint32_t STATUS_EDGES_MIN = 30;  // ~75Hz min (50% margin below 110Hz)

void IRAM_ATTR onStatusEdge() {
    statusEdgeCount++;
}

// Sample the edge counter over a window, return true if signal present.
// Non-blocking: pass elapsed window in ms, returns true if last window
// crossed STATUS_EDGES_MIN. Reset internal accumulator each call.
bool statusSignalLive() {
    static uint32_t lastSampleMs = 0;
    static uint32_t lastCount = 0;
    uint32_t now = millis();
    if (now - lastSampleMs < STATUS_WINDOW_MS) return false;  // not yet
    uint32_t cur = statusEdgeCount;
    uint32_t delta = cur - lastCount;
    lastCount = cur;
    lastSampleMs = now;
    return delta >= STATUS_EDGES_MIN;
}

// Reset the status sampler (call when starting a new bring-up sequence)
void statusSamplerReset() {
    noInterrupts();
    statusEdgeCount = 0;
    interrupts();
}

// ---- duty / command primitives ----
void setDuty(int d){
    if(d<DUTY_MIN) d=DUTY_MIN;
    if(d>DUTY_MAX) d=DUTY_MAX;
    curDuty=d;
    sinkTicks=(uint32_t)d*periodTicks/100;
}

// ---- Captured-profile playback (the 120V oven command that actually struck) ----
// Recorded oven_run1.txt: 221.8 Hz (4509us period), HIGH ~85% warmup -> ~87.5%
// sustained. Reproduced with PB_PERIOD ticks; tick_us derived from pbFreqHz so
// the period is accurate (no integer-frequency truncation). All ADJUSTABLE live
// (serial or web). GPIO5 stays READ-ONLY; open-loop, no status gate.
constexpr uint32_t PB_PERIOD = 167;      // ticks/period -> 0.6% duty resolution
float    pbFreqHz   = 221.8f;            // proven recorded frequency
float    pbWarmPct  = 85.0f;             // warmup HIGH % (captured)
float    pbRunPct   = 87.5f;             // sustained HIGH % (captured)
uint32_t pbWarmupMs = 4000;              // hold warmup this long, then step to run
bool     playbackActive = false;
bool     pbInRun = false;
uint32_t pbStartMs = 0;

// tick period (us) for the chosen playback frequency, and HIGH-ticks for a duty %
uint32_t pbTickUs(){
    float f = pbFreqHz; if(f<50) f=50; if(f>400) f=400;
    long u = lroundf(1000000.0f/(f*PB_PERIOD)); if(u<1) u=1; return (uint32_t)u;
}
uint32_t pbTicks(float pct){
    if(pct<10) pct=10; if(pct>95) pct=95;          // clamp; never 0 or 100% (no off-time)
    long t = lroundf(pct/100.0f*PB_PERIOD);
    if(t<1) t=1; if(t>=(long)PB_PERIOD) t=PB_PERIOD-1; return (uint32_t)t;
}

void cmdOff(){
    drvEnabled=false; sinkTicks=0;
    playbackActive=false; pbInRun=false;
    periodTicks=TICKS;                   // restore normal duty resolution
    GPIO.out_w1tc=(1u<<PIN_PWM);
}

void cmdOn(){
    tickCounter=0; drvEnabled=true;
    timerStart(cmdFreqHz);
}

bool startPlayback(){
    // open-loop playback of the captured 120V command profile (adjustable params)
    periodTicks = PB_PERIOD;
    sinkTicks   = pbTicks(pbWarmPct);
    curDuty     = (int)lroundf(pbWarmPct);
    tickCounter = 0; drvEnabled = true;
    timerStartUs(pbTickUs());
    playbackActive = true; pbInRun = false; pbStartMs = millis();
    running = true; runStartMs = millis();
    Serial.printf("[PLAY] %.1fHz: warmup %.1f%% hold %lums -> run %.1f%% sustained. "
                  "GPIO5 read-only, open-loop. 'off' to stop.\n",
                  pbFreqHz, pbWarmPct, (unsigned long)pbWarmupMs, pbRunPct);
    return true;
}

// ============================================================================
//  STARTUP STATE MACHINE (VERIFIED — no soft-start)
//  Phase A: STRIKE — drive curRunDuty (default 75% = VK3HZ P100) from cycle
//                    PIN_STATUS for the 110Hz signal (magnetron drawing current)
//  Phase B: RUN    — status seen → hold curRunDuty until stopRun
//  Phase C: ABORT  — no status within timeout → stop, report
//
//  Bypasses:
//    forceMode    — skip status detection entirely, run open-loop. This is the
//                   proven single-IGBT path; use it if the status divider on
//                   GPIO5 isn't wired (e.g. ORANGE tied to GND through 1k).
//    statusBypass — treat strike as immediately complete (divider-debug only)
// ============================================================================

// Returns true if startup succeeded (in RUN), false if aborted.
// Blocking — caller decides when to return.
bool startRun(){
    cmdFreqHz = FREQ_LIST[freqIdx];

    if(forceMode){
        Serial.println("[ON] FORCE mode: open-loop, no status check");
        setDuty(curRunDuty);
        cmdOn();
        running = true;
        return true;
    }

    // Phase A: STRIKE — drive the run duty from cycle one (NO soft-start, per
    // captured oven command). Inverter's own current loop handles inrush. We
    // hold this same duty and wait for the 110Hz status signal to confirm the
    // magnetron has struck and is drawing current.
    Serial.printf("[ON] driving %d%% @ %luHz from cycle one, waiting up to %lums for status...\n",
                  curRunDuty, (unsigned long)cmdFreqHz, (unsigned long)statusTimeoutMs);
    statusSamplerReset();
    setDuty(curRunDuty);
    cmdOn();
    running = true;

    if(statusBypass){
        Serial.println("[ON] NOSTATUS bypass: skipping status detection, treating as RUN");
        Serial.printf("[RUN] %d%% @ %luHz (status bypassed)\n",
                      curRunDuty, (unsigned long)cmdFreqHz);
        return true;
    }

    // Poll for status signal
    uint32_t deadline = millis() + statusTimeoutMs;
    bool statusFound = false;
    while(running && millis() < deadline){
        if(statusSignalLive()){
            statusFound = true;
            break;
        }
        delay(5);
    }

    if(!statusFound){
        // Phase C: ABORT
        cmdOff();
        running = false;
        Serial.printf("[ABORT] no status signal in %lums.\n",
                      (unsigned long)statusTimeoutMs);
        Serial.println("[ABORT] check: magnetron present? mains 240V? wiring? divider on GPIO5?");
        Serial.println("[ABORT] to bypass status check: 'nostatus' (debug) or 'force' (open-loop, risky)");
        return false;
    }

    // Phase B: RUN — status confirms the magnetron struck. We're already at
    // run duty (drove it from cycle one), so just confirm and hold.
    uint32_t strikeMs = statusTimeoutMs - (deadline - millis());
    Serial.printf("[STATUS LIVE] magnetron oscillating after %lums\n",
                  (unsigned long)strikeMs);
    Serial.printf("[RUN] %d%% @ %luHz\n", curRunDuty, (unsigned long)cmdFreqHz);
    return true;
}

void stopRun(){
    cmdOff();
    running = false;
    webControlled = false;
    Serial.println("[OFF] command=0.");
}

// Forward decl + serial buffer (declared early so pulseRun() can service serial)
String buf;
void handle(String c);

// ---- TIMED PULSE — bounded burst for bring-up ----
constexpr uint32_t PULSE_MAX_MS = 30000;

void pulseRun(uint32_t ms){
    if(ms < 1) ms = 1;
    if(ms > PULSE_MAX_MS){
        ms = PULSE_MAX_MS;
        Serial.printf("[PULSE] clamped to max %lums\n",(unsigned long)PULSE_MAX_MS);
    }
    Serial.printf("[PULSE] %lums at run duty after warmup. 'off' aborts.\n",
                  (unsigned long)ms);
    if(!startRun()){
        Serial.println("[PULSE] aborted during warmup, no fire");
        return;
    }
    uint32_t deadline = millis() + ms;
    while(running && (int32_t)(deadline - millis()) > 0){
        while(Serial.available()){
            char ch=(char)Serial.read();
            if(ch=='\n'||ch=='\r'){ if(buf.length()){handle(buf);buf="";} }
            else if(buf.length()<32) buf+=ch;
        }
        delay(5);
    }
    if(running){
        Serial.printf("[PULSE] %lums elapsed -> auto-stop\n",(unsigned long)ms);
        stopRun();
    } else {
        Serial.println("[PULSE] aborted before deadline");
    }
}

// ============================================================================
//  COMMAND-LINE LOGGER (capture mode for GPIO6)
// ============================================================================
#define LOG_MAXEDGES 8000
#define BOOT_AUTOLOG 0
constexpr uint32_t LOG_WINDOW_MS = 10000;

volatile uint32_t logT[LOG_MAXEDGES];
volatile uint8_t  logL[LOG_MAXEDGES];
volatile uint32_t logIdx = 0;
volatile bool     logArmed = false;
volatile uint32_t logFirstUs = 0;

void IRAM_ATTR onLogEdge(){
    if(!logArmed || logIdx>=LOG_MAXEDGES) return;
    uint32_t t=micros();
    if(logIdx==0) logFirstUs=t;
    logT[logIdx]=t;
    logL[logIdx]=(uint8_t)digitalRead(PIN_LOG);
    logIdx++;
}

void logDump(){
    logArmed=false;
    detachInterrupt(PIN_LOG);
    if(!logIdx){ Serial.println("[log] no edges captured."); return; }
    uint32_t base=logT[0];
    Serial.println("---RAW BEGIN---");
    Serial.flush();
    Serial.println("idx,t_us,level,dt_us");
    for(uint32_t i=0;i<logIdx;i++){
        Serial.printf("%lu,%lu,%u,%lu\n",(unsigned long)i,
            (unsigned long)(logT[i]-base),logL[i],
            (unsigned long)(i?logT[i]-logT[i-1]:0));
        if((i & 0x1F)==0){ Serial.flush(); delay(2); }
    }
    Serial.flush();
    Serial.printf("---RAW END--- %lu edges\n",(unsigned long)logIdx);
    Serial.println("===CAPTURE DONE===");
    Serial.flush();
}

void logArm(){
    if(running) stopRun();
    logIdx=0; logFirstUs=0; logArmed=true;
    pinMode(PIN_LOG, INPUT);
    attachInterrupt(PIN_LOG, onLogEdge, CHANGE);
    Serial.println("[log] ARMED on GPIO6.");
}

// ---- buttons ----
unsigned long lastB1=0, lastB2=0;
constexpr unsigned long DEBOUNCE_MS=200;
int prevB1=HIGH, prevB2=HIGH;

void btnOnOff(){ if(running) stopRun(); else startPlayback(); }
void btnPower(){
    runIdx=(runIdx+1)%RUN_COUNT;
    curRunDuty=RUN_STEPS[runIdx];
    if(running){ setDuty(curRunDuty); }
    Serial.printf("[BTN2] run power -> %d%%%s\n", curRunDuty, running?" (live)":" (set; BTN1 to run)");
}
void pollButtons(){
    int b1=digitalRead(PIN_BTN_ONOFF), b2=digitalRead(PIN_BTN_POWER);
    unsigned long now=millis();
    if(prevB1==HIGH && b1==LOW && now-lastB1>DEBOUNCE_MS){ lastB1=now; btnOnOff(); }
    if(prevB2==HIGH && b2==LOW && now-lastB2>DEBOUNCE_MS){ lastB2=now; btnPower(); }
    prevB1=b1; prevB2=b2;
}

// ---- handle command ----
void handle(String c){
    c.trim(); if(!c.length()) return;
    String lc=c; lc.toLowerCase();
    if(lc=="?"||lc=="help"){
        Serial.println(F("on off | play | pwarm <ms> | pulse <ms> | p<10-75> | f<hz>"));
        Serial.println(F("tmo <ms> | force | nostatus"));
        Serial.println(F("statusinfo | log logdump | zc status"));
        Serial.println(F("play = captured 120V profile (85->87.5% @221.8Hz, GPIO5 read-only)."));
        Serial.println(F("Cap 75% on p<>; play bypasses cap (uses recorded ticks). 'pulse 500' bounded."));
    }
    else if(lc=="on"){ startRun(); }
    else if(lc=="off"){ stopRun(); }
    else if(lc=="play"){ startPlayback(); }
    else if(lc.startsWith("pwarm")){
        int sp = c.indexOf(' ');
        if(sp>0){
            long m = c.substring(sp+1).toInt();
            if(m>=500 && m<=20000){ pbWarmupMs=m;
                Serial.printf("[pwarm] playback warmup hold = %lums\n",(unsigned long)pbWarmupMs); }
            else Serial.println("[pwarm] usage: pwarm <500..20000>");
        } else Serial.printf("[pwarm] current=%lums\n",(unsigned long)pbWarmupMs);
    }
    else if(lc.startsWith("pulse")){
        long ms = c.substring(5).toInt();
        if(ms <= 0){ Serial.println("[PULSE] usage: pulse <ms>  e.g. 'pulse 500'"); }
        else { pulseRun((uint32_t)ms); }
    }
    else if(lc.startsWith("tmo")){
        int sp = c.indexOf(' ');
        if(sp>0){
            long m = c.substring(sp+1).toInt();
            if(m>=500 && m<=30000){ statusTimeoutMs=m;
                Serial.printf("[tmo] %lums\n",(unsigned long)statusTimeoutMs); }
            else Serial.println("[tmo] usage: tmo <500..30000>");
        } else Serial.printf("[tmo] current=%lums\n",(unsigned long)statusTimeoutMs);
    }
    else if(lc=="force"){ forceMode = !forceMode;
        Serial.printf("[force] %s (open-loop, no status gate)\n",
            forceMode?"ENABLED — DANGEROUS":"disabled"); }
    else if(lc=="nostatus"){ statusBypass = !statusBypass;
        Serial.printf("[nostatus] %s\n", statusBypass?"ENABLED (debug)":"disabled"); }
    else if(lc=="statusinfo"){
        // Quick live readout: count edges over 200ms now
        uint32_t before = statusEdgeCount;
        delay(200);
        uint32_t edges = statusEdgeCount - before;
        Serial.printf("[statusinfo] %lu edges in 200ms (~%.1fHz). %s threshold (%lu)\n",
            (unsigned long)edges, edges*2.5,
            edges >= STATUS_EDGES_MIN ? ">=" : "<",
            (unsigned long)STATUS_EDGES_MIN);
        Serial.printf("[statusinfo] PIN_STATUS instantaneous: %d\n", digitalRead(PIN_STATUS));
    }
    else if(lc=="log"){ logArm(); }
    else if(lc=="logdump"){ logDump(); }
    else if(lc=="status"){
        Serial.printf("[status] run=%d duty=%d%% freq=%luHz tmo=%lums force=%d nostatus=%d\n",
            running, curDuty, (unsigned long)cmdFreqHz,
            (unsigned long)statusTimeoutMs,
            forceMode, statusBypass);
    }
    else if(lc=="zc"){
        Serial.printf("[zc] period=%luus ~%.1fHz (%s)\n",
            (unsigned long)zcPeriodUs,
            zcPeriodUs?1000000.0/zcPeriodUs:0.0,
            zcSeen?"live":"none");
    }
    else if(lc.startsWith("p")){
        int d=c.substring(1).toInt();
        if(d > DUTY_MAX){
            Serial.printf("[p] %d > %d REJECTED (VK3HZ P100 = %d%%, >cap over-drives)\n", d, DUTY_MAX, DUTY_MAX);
        } else {
            curRunDuty=d; setDuty(d);
            Serial.printf("[p] run duty=%d%%\n",curDuty);
        }
    }
    else if(lc.startsWith("f")){
        long h=c.substring(1).toInt();
        if(h>0){cmdFreqHz=h; if(running)timerStart(h);
            Serial.printf("[f]%luHz\n",(unsigned long)cmdFreqHz);}
    }
    else Serial.println("? (try ?)");
}

// ============================================================================
//  WiFi AP web UI  —  adjustable params + live feedback, PWM unaffected
// ============================================================================
const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>Termite RF</title><style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:14px}
h2{margin:.2em 0}.card{background:#1c1c1c;border:1px solid #333;border-radius:10px;padding:12px;margin:10px 0}
.row{display:flex;justify-content:space-between;align-items:center;margin:6px 0}
input[type=number]{width:90px;background:#222;color:#eee;border:1px solid #444;border-radius:6px;padding:6px}
button{font-size:1.05em;border:0;border-radius:10px;padding:14px;margin:4px 0;width:100%;color:#fff;cursor:pointer}
.play{background:#1f7a3f}.stop{background:#a11;font-size:1.4em;padding:20px}.apply{background:#2563a8}
.k{color:#9af}.v{font-weight:600}.big{font-size:1.3em}
label{display:flex;justify-content:space-between;align-items:center;margin:6px 0}
small{color:#888}
</style></head><body>
<h2>Termite RF — inverter control</h2>
<div class=card>
 <div class=row><span class=k>State</span><span class="v big" id=mode>—</span></div>
 <div class=row><span class=k>Duty</span><span class=v id=duty>—</span></div>
 <div class=row><span class=k>Freq</span><span class=v id=freq>—</span></div>
 <div class=row><span class=k>Status 110Hz</span><span class=v id=shz>—</span></div>
 <div class=row><span class=k>Run time</span><span class=v id=rt>—</span></div>
</div>
<button class=play onclick="go('/play')">PLAY  (warmup &rarr; run)</button>
<button class=stop onclick="go('/off')">STOP</button>
<div class=card>
 <h3>Adjust</h3>
 <label>Freq (Hz)<input type=number id=i_freq step=0.1></label>
 <label>Warmup %<input type=number id=i_warm step=0.1></label>
 <label>Run %<input type=number id=i_run step=0.1></label>
 <label>Warmup hold (ms)<input type=number id=i_wms step=100></label>
 <label>Dead-man auto-off<input type=checkbox id=i_dm></label>
 <label>Dead-man (s)<input type=number id=i_dms></label>
 <label>Max run (s)<input type=number id=i_mr></label>
 <button class=apply onclick="apply()">Apply</button>
 <small>Dead-man off = WiFi hiccup won't abort a run (good for tuning). Max-run always caps web runs.</small>
</div>
<script>
function go(u){fetch(u).then(refresh)}
function apply(){
 let q='/set?freq='+v('i_freq')+'&warm='+v('i_warm')+'&run='+v('i_run')
  +'&warmms='+v('i_wms')+'&deadman='+(document.getElementById('i_dm').checked?1:0)
  +'&dmsec='+v('i_dms')+'&maxrun='+v('i_mr');
 fetch(q).then(refresh);
}
function v(id){return document.getElementById(id).value}
function refresh(){fetch('/state').then(r=>r.json()).then(s=>{
 mode.textContent=s.mode.toUpperCase(); mode.style.color=s.run?'#5d5':'#888';
 duty.textContent=s.duty+' %'; freq.textContent=s.freq+' Hz';
 shz.textContent=s.shz+' Hz '+(s.spresent?'PRESENT':'absent');
 rt.textContent=s.run?(s.rt+' s'):'—';
 if(document.activeElement.tagName!='INPUT'){
  i_freq.value=s.freq;i_warm.value=s.warm;i_run.value=s.runp;i_wms.value=s.wms;
  i_dm.checked=s.dm;i_dms.value=s.dms;i_mr.value=s.mr;}
})}
setInterval(refresh,1000); refresh();
</script></body></html>
)HTML";

const char* modeStr(){
    if(!running) return "idle";
    if(playbackActive) return pbInRun ? "run" : "warmup";
    if(forceMode) return "force";
    return "run";
}

void handleRoot(){ server.send_P(200,"text/html",PAGE); }

void handleState(){
    lastWebContactMs = millis();                       // keep-alive
    uint32_t rt = running ? (millis()-runStartMs)/1000 : 0;
    bool spresent = (statusHzX10 >= 750);              // ~75Hz+ => status live
    char j[420];
    snprintf(j,sizeof(j),
      "{\"run\":%d,\"mode\":\"%s\",\"duty\":%d,\"freq\":%.1f,"
      "\"warm\":%.1f,\"runp\":%.1f,\"wms\":%lu,\"shz\":%.1f,\"spresent\":%d,"
      "\"rt\":%lu,\"dm\":%d,\"dms\":%lu,\"mr\":%lu}",
      running?1:0, modeStr(), curDuty, pbFreqHz,
      pbWarmPct, pbRunPct, (unsigned long)pbWarmupMs,
      statusHzX10/10.0, spresent?1:0,
      (unsigned long)rt, deadmanOn?1:0,(unsigned long)deadmanSec,(unsigned long)maxRunSec);
    server.send(200,"application/json",j);
}

void handleSet(){
    if(server.hasArg("freq"))   pbFreqHz   = server.arg("freq").toFloat();
    if(server.hasArg("warm"))   pbWarmPct  = server.arg("warm").toFloat();
    if(server.hasArg("run"))    pbRunPct   = server.arg("run").toFloat();
    if(server.hasArg("warmms")){ long m=server.arg("warmms").toInt(); if(m>=0&&m<=20000) pbWarmupMs=m; }
    if(server.hasArg("deadman")) deadmanOn = server.arg("deadman").toInt()!=0;
    if(server.hasArg("dmsec")){ long s=server.arg("dmsec").toInt(); if(s>=3&&s<=120) deadmanSec=s; }
    if(server.hasArg("maxrun")){ long s=server.arg("maxrun").toInt(); if(s>=3&&s<=600) maxRunSec=s; }
    // live-apply duty if a playback is already running
    if(playbackActive){ sinkTicks = pbTicks(pbInRun?pbRunPct:pbWarmPct); curDuty=(int)lroundf(pbInRun?pbRunPct:pbWarmPct); }
    Serial.printf("[SET] freq=%.1f warm=%.1f run=%.1f wms=%lu dm=%d dms=%lu maxrun=%lu\n",
        pbFreqHz,pbWarmPct,pbRunPct,(unsigned long)pbWarmupMs,deadmanOn?1:0,
        (unsigned long)deadmanSec,(unsigned long)maxRunSec);
    server.send(200,"text/plain","ok");
}

void setupWeb(){
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    server.on("/", handleRoot);
    server.on("/state", handleState);
    server.on("/set", handleSet);
    server.on("/play", [](){ startPlayback(); webControlled=true; runStartMs=millis();
                             lastWebContactMs=millis(); server.send(200,"text/plain","ok"); });
    server.on("/off",  [](){ stopRun(); server.send(200,"text/plain","ok"); });
    server.begin();
    Serial.printf("[WiFi] AP '%s' (pass '%s')  ->  http://192.168.4.1/\n", AP_SSID, AP_PASS);
}

// safety + status-rate sampling, called from loop()
void webTick(){
    server.handleClient();
    // sample status edge rate every 500ms -> Hz (110Hz/50% = 220 edges/s)
    static uint32_t lastS=0, lastEdges=0;
    uint32_t now=millis();
    if(now-lastS>=500){
        uint32_t e=statusEdgeCount; uint32_t d=e-lastEdges; lastEdges=e;
        float hz = (d/ (float)(now-lastS) *1000.0f)/2.0f;   // edges/s /2 = Hz
        statusHzX10 = (uint32_t)lroundf(hz*10.0f);
        lastS=now;
    }
    // safety auto-off (web-initiated runs only; attended serial/button runs unaffected)
    if(running && webControlled){
        if(maxRunSec>0 && (now-runStartMs) > maxRunSec*1000UL){
            stopRun(); Serial.println("[SAFE] max-run reached -> OFF"); }
        else if(deadmanOn && (now-lastWebContactMs) > deadmanSec*1000UL){
            stopRun(); Serial.println("[SAFE] deadman: lost web contact -> OFF"); }
    }
}

void setup(){
    Serial.begin(115200);
    pinMode(PIN_PWM, OUTPUT); digitalWrite(PIN_PWM, LOW);
    GPIO.out_w1tc=(1u<<PIN_PWM);

    // PIN_STATUS read-only. INPUT_PULLDOWN so a DISCONNECTED status line can't
    // float and fire spurious edge interrupts (esp. under RF). With the external
    // 1k/1k divider connected, the ~45k internal pulldown is negligible.
    pinMode(PIN_STATUS, INPUT_PULLDOWN);
    attachInterrupt(PIN_STATUS, onStatusEdge, CHANGE);

    cmdOff();
    pinMode(PIN_ZC, INPUT);
    attachInterrupt(PIN_ZC, onZeroCross, RISING);
    pinMode(PIN_BTN_ONOFF, INPUT_PULLUP);
    pinMode(PIN_BTN_POWER, INPUT_PULLUP);
    cmdFreqHz=FREQ_LIST[freqIdx]; curRunDuty=RUN_STEPS[runIdx]; setDuty(curRunDuty);
    delay(200);
    Serial.println("\n===============================================");
    Serial.println(" Panasonic 2-IGBT inverter driver (F6645M family, 240V)");
    Serial.println(" VERIFIED: drive run duty from cycle one -> wait 110Hz status -> run");
    Serial.printf ( " Run: %d%% / TMO: %lums / Freq: %luHz (no soft-start)\n",
        curRunDuty,
        (unsigned long)statusTimeoutMs, (unsigned long)cmdFreqHz);
    Serial.println(" GPIO4->YELLOW(cmd)  GPIO5<-ORANGE(status,via divider)  GND->BROWN");
    Serial.println(" REQUIRED for status read: 1k series + ~1k pulldown on GPIO5");
    Serial.println(" 'on' or 'pulse 500'. 'force' = open-loop (proven single-IGBT path).");
    Serial.println("===============================================");
#if BOOT_AUTOLOG
    delay(500);
    Serial.println("[AUTOLOG] capture build. cat /dev/ttyACM0 > run.txt");
    logArm();
#endif
    setupWeb();   // AP 'TermiteRF' / http://192.168.4.1/ — untethered control
}

void loop(){
    while(Serial.available()){
        char ch=(char)Serial.read();
        if(ch=='\n'||ch=='\r'){ if(buf.length()){handle(buf);buf="";} }
        else if(buf.length()<32) buf+=ch;
    }
    pollButtons();
    // captured-profile playback: after the warmup hold, step from ~85% to ~87.5%
    if(playbackActive && !pbInRun && (millis()-pbStartMs) >= pbWarmupMs){
        sinkTicks = pbTicks(pbRunPct);
        curDuty   = (int)lroundf(pbRunPct);
        pbInRun   = true;
        Serial.printf("[PLAY] warmup done (%lums) -> run %.1f%% HIGH sustained\n",
                      (unsigned long)pbWarmupMs, pbRunPct);
    }
    if(logArmed && logIdx>=LOG_MAXEDGES){ Serial.println("[log] buffer full."); logDump(); }
    if(logArmed && logIdx>0 && (micros()-logFirstUs) > LOG_WINDOW_MS*1000UL){
        Serial.println("[log] window elapsed."); logDump();
    }
    webTick();        // serve web UI + status-rate sampling + safety auto-off
    delay(2);
}
