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
 *  2) STARTUP STATE MACHINE per VK3HZ documentation:
 *     A) WARMUP: send 50% duty @ 222Hz, watch PIN_STATUS for 110Hz signal
 *     B) RUN:    status seen -> drop to commanded duty per VK3HZ table
 *     C) ABORT:  no status in STATUS_TIMEOUT_MS -> stop immediately
 *
 *  3) MAX DUTY CAPPED AT 75% — per VK3HZ table, P100 = 75% duty.
 *     The old "ramp to 70%" was actually equivalent to P80-P90 which is
 *     above-spec drive. Likely cause of off-resonance overcurrent failure.
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
 *    on / off          start (warmup then run, status-gated) / stop
 *    pulse <ms>        timed burst (1..30000ms at run duty, auto-stop).
 *                      Goes through warmup. 'off' aborts early.
 *    p <10..75>        set RUN duty % (HARD CAP 75% per VK3HZ P100)
 *    f <hz>            set command frequency (default 222)
 *    warmup <duty>     set warmup duty % (default 50, per VK3HZ findings)
 *    tmo <ms>          set status-timeout ms (default 5000)
 *    force             ignore status signal — open-loop run (DANGEROUS)
 *    nostatus          stop trying to read status, treat as warmup-complete
 *    statusinfo        print live status-signal edge rate
 *    log / logdump     capture mode on GPIO6
 *    zc / status / ?
 * ==========================================================================*/

#include "soc/gpio_struct.h"

constexpr int PIN_PWM     = 4;   // command out -> CN701 pin1 (YELLOW)
constexpr int PIN_STATUS  = 5;   // status IN  <- CN701 pin3 (ORANGE), via divider
constexpr int PIN_ZC      = 7;   // zero-cross monitor
constexpr int PIN_BTN_ONOFF = 1;
constexpr int PIN_BTN_POWER = 2;
constexpr int PIN_LOG     = 6;   // capture tap

constexpr bool INVERT_CMD = false;

constexpr uint32_t TICKS = 200;        // 0.5% duty resolution
uint32_t cmdFreqHz = 222;              // 220Hz command (VK3HZ + service guide)
int      curDuty   = 50;
bool     running   = false;

// ---- VK3HZ-derived parameters ----
// Warm-up: send 50% duty until the 110Hz status signal appears, then drop
// to commanded duty. If status never appears within STATUS_TIMEOUT_MS, abort.
int      warmupDuty       = 50;        // VK3HZ: oven sends 50% during warm-up
uint32_t statusTimeoutMs  = 5000;      // 5s to see status before aborting

// Max duty hard cap per VK3HZ table (P100 = 75% duty)
constexpr int DUTY_MIN   = 10;
constexpr int DUTY_MAX   = 75;         // WAS 95 — VK3HZ P100 = 75% duty

// RUN power steps. Conservative ladder. NOTHING above 75.
const int RUN_STEPS[] = {40, 50, 55, 60, 65, 70, 75};
const int RUN_COUNT = sizeof(RUN_STEPS)/sizeof(RUN_STEPS[0]);
int runIdx = 0;                        // DEFAULT 40% (lowest VK3HZ continuous)
int curRunDuty = 40;

const uint32_t FREQ_LIST[] = {110, 150, 165, 180, 200, 222, 233, 250, 280, 300};
const int FREQ_COUNT = sizeof(FREQ_LIST)/sizeof(FREQ_LIST[0]);
int freqIdx = 5;                       // 222Hz

// Override flags
bool forceMode    = false;             // 'force' = ignore status, open-loop run
bool statusBypass = false;             // 'nostatus' = skip status detection

// ---- ISR PWM ----
hw_timer_t* pwmTimer = nullptr;
volatile uint32_t tickCounter = 0, sinkTicks = 0;
volatile bool drvEnabled = false;

void IRAM_ATTR onPwmTick() {
    if (!drvEnabled) { GPIO.out_w1tc = (1u<<PIN_PWM); return; }
    tickCounter++;
    if (tickCounter >= TICKS) tickCounter = 0;
    bool on = (tickCounter < sinkTicks);
    if (INVERT_CMD) on = !on;
    if (on) GPIO.out_w1ts = (1u<<PIN_PWM);
    else    GPIO.out_w1tc = (1u<<PIN_PWM);
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
    sinkTicks=(uint32_t)d*TICKS/100;
}

void cmdOff(){
    drvEnabled=false; sinkTicks=0;
    GPIO.out_w1tc=(1u<<PIN_PWM);
}

void cmdOn(){
    tickCounter=0; drvEnabled=true;
    timerStart(cmdFreqHz);
}

// ============================================================================
//  STARTUP STATE MACHINE
//  Phase A: WARMUP — send warmupDuty (50%), watch PIN_STATUS for 110Hz signal
//  Phase B: RUN    — status seen → drop to curRunDuty, hold until stopRun
//  Phase C: ABORT  — no status within timeout → stop, report
//
//  Bypasses:
//    forceMode    — skip status detection entirely, run open-loop (DANGEROUS)
//    statusBypass — treat warmup as immediately complete (for hardware-divider
//                   debugging when status signal isn't readable yet)
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

    // Phase A: WARMUP
    Serial.printf("[ON] WARMUP %d%% @ %luHz, waiting up to %lums for status signal...\n",
                  warmupDuty, (unsigned long)cmdFreqHz, (unsigned long)statusTimeoutMs);
    statusSamplerReset();
    setDuty(warmupDuty);
    cmdOn();
    running = true;

    if(statusBypass){
        Serial.println("[ON] NOSTATUS bypass: skipping status detection, going to RUN");
        delay(2000);  // still give magnetron filament some warm-up time
        setDuty(curRunDuty);
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

    // Phase B: RUN
    uint32_t warmupMs = statusTimeoutMs - (deadline - millis());
    Serial.printf("[STATUS LIVE] magnetron oscillating after %lums warmup\n",
                  (unsigned long)warmupMs);
    setDuty(curRunDuty);
    Serial.printf("[RUN] %d%% @ %luHz\n", curRunDuty, (unsigned long)cmdFreqHz);
    return true;
}

void stopRun(){
    cmdOff();
    running = false;
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

void btnOnOff(){ if(running) stopRun(); else startRun(); }
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
        Serial.println(F("on off | pulse <ms> | p<10-75> | f<hz>"));
        Serial.println(F("warmup <duty> | tmo <ms> | force | nostatus"));
        Serial.println(F("statusinfo | log logdump | zc status"));
        Serial.println(F("VK3HZ MODE: warmup 50%, wait for 110Hz status, then run."));
        Serial.println(F("Max duty 75% (VK3HZ P100). 'pulse 500' = safest first-use."));
    }
    else if(lc=="on"){ startRun(); }
    else if(lc=="off"){ stopRun(); }
    else if(lc.startsWith("pulse")){
        long ms = c.substring(5).toInt();
        if(ms <= 0){ Serial.println("[PULSE] usage: pulse <ms>  e.g. 'pulse 500'"); }
        else { pulseRun((uint32_t)ms); }
    }
    else if(lc.startsWith("warmup")){
        int sp = c.indexOf(' ');
        if(sp>0){
            int d = c.substring(sp+1).toInt();
            if(d>=10 && d<=75){ warmupDuty=d;
                Serial.printf("[warmup] %d%%\n", warmupDuty); }
            else Serial.println("[warmup] usage: warmup <10..75>");
        } else Serial.printf("[warmup] current=%d%%\n", warmupDuty);
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
        Serial.printf("[status] run=%d duty=%d%% freq=%luHz warmup=%d%% tmo=%lums force=%d nostatus=%d\n",
            running, curDuty, (unsigned long)cmdFreqHz,
            warmupDuty, (unsigned long)statusTimeoutMs,
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
        if(d > 75){
            Serial.printf("[p] %d > 75 REJECTED — VK3HZ P100 = 75%% max\n", d);
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

void setup(){
    Serial.begin(115200);
    pinMode(PIN_PWM, OUTPUT); digitalWrite(PIN_PWM, LOW);
    GPIO.out_w1tc=(1u<<PIN_PWM);

    // PIN_STATUS as plain INPUT — external 2.2k pulldown forms divider with
    // 1k series resistor against board's 5V pull-up to give safe ~3.0V hi.
    pinMode(PIN_STATUS, INPUT);
    attachInterrupt(PIN_STATUS, onStatusEdge, CHANGE);

    cmdOff();
    pinMode(PIN_ZC, INPUT);
    attachInterrupt(PIN_ZC, onZeroCross, RISING);
    pinMode(PIN_BTN_ONOFF, INPUT_PULLUP);
    pinMode(PIN_BTN_POWER, INPUT_PULLUP);
    cmdFreqHz=FREQ_LIST[freqIdx]; curRunDuty=RUN_STEPS[runIdx]; setDuty(curRunDuty);
    delay(200);
    Serial.println("\n===============================================");
    Serial.println(" Panasonic 2-IGBT inverter driver (F6645M301GP / M3GP)");
    Serial.println(" VK3HZ MODE: warmup -> wait for 110Hz status -> run");
    Serial.printf ( " Warmup: %d%% / Run: %d%% / TMO: %lums / Freq: %luHz\n",
        warmupDuty, curRunDuty,
        (unsigned long)statusTimeoutMs, (unsigned long)cmdFreqHz);
    Serial.println(" GPIO4->pin1(YELLOW)  GPIO5<-pin3(ORANGE,status)  GND->pin2");
    Serial.println(" REQUIRED: 2.2k pulldown GPIO5->GND for safe 5V reading");
    Serial.println(" 'on' or 'pulse 500'. 'statusinfo' to check status signal first.");
    Serial.println("===============================================");
#if BOOT_AUTOLOG
    delay(500);
    Serial.println("[AUTOLOG] capture build. cat /dev/ttyACM0 > run.txt");
    logArm();
#endif
}

void loop(){
    while(Serial.available()){
        char ch=(char)Serial.read();
        if(ch=='\n'||ch=='\r'){ if(buf.length()){handle(buf);buf="";} }
        else if(buf.length()<32) buf+=ch;
    }
    pollButtons();
    if(logArmed && logIdx>=LOG_MAXEDGES){ Serial.println("[log] buffer full."); logDump(); }
    if(logArmed && logIdx>0 && (micros()-logFirstUs) > LOG_WINDOW_MS*1000UL){
        Serial.println("[log] window elapsed."); logDump();
    }
    delay(2);
}
