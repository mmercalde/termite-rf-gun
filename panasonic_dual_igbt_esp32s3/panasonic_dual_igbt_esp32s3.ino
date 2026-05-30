/* ============================================================================
 *  panasonic_dual_igbt_esp32s3.ino  —  Panasonic 2-IGBT inverter driver (EXPERIMENTAL)
 *  ESP32-S3 Dev Module · core 2.x/3.x · 4MB · PSRAM off · USB CDC On Boot
 * ----------------------------------------------------------------------------
 *  TARGET BOARD: Panasonic F6645M301GP / "M3GP" 2-IGBT inverter
 *               (originally Toshiba GT50J327 big + GT35J321 small — both
 *                obsolete; replaced with Infineon IHW40N120R5 big +
 *                IHW30N120R5 small, both TRENCHSTOP RC-H5, 1200 V),
 *               CN701 3-pin command/feedback, CN703 HV out.
 *
 *  HOW THIS DIFFERS FROM THE SINGLE-IGBT BUILD (panasonic_inverter_esp32s3):
 *   The single-IGBT board ran on a flat 222Hz / 85% command from cycle one,
 *   no soft-start, pin3 tied to 1k->GND. The 2-IGBT controller is more
 *   protective: bench symptom is "press start, IGBT fires for a few ms, then
 *   the controller gates off" — identical on two boards, with both boards'
 *   IGBTs measured GOOD (E->C 0.451V, C->E OL, G-E ~42k matched). So the power
 *   stage is healthy and this is a START/COMMAND problem, not hardware.
 *
 *   TWO changes vs the single-IGBT build, both grounded in the primary research
 *   (VK3HZ teardown + Panasonic service guide), targeting the ms-abort:
 *     1) SOFT-START: open at SS_DUTY (~35%) and hold SS_MS (~2s) BEFORE ramping
 *        to run power. The oven's DPC does exactly this warm-up; the 2-IGBT
 *        controller appears to require it (slamming 85% cold = instant trip).
 *     2) PIN-3 FEEDBACK (GPIO5) ENABLED BY DEFAULT: drives the 110Hz/50%
 *        phase-locked status pattern the controller may want to "see". On the
 *        single-IGBT board this wasn't needed; here it's the prime second
 *        suspect if soft-start alone doesn't sustain.
 *
 *  PRIMARY-SOURCE COMMAND FACTS (NOT the 20-40kHz myth):
 *    - CN701 pin1 command is LOW-FREQUENCY ~220Hz TTL PWM (VK3HZ scope; the
 *      teardown video's yellow trace; service manuals' "~3VAC on a meter" =
 *      averaged 220Hz square wave). The 20-40kHz is the IGBT GATE switching
 *      the board's OWN controller generates internally, NOT what CN701 takes.
 *    - pin3 = 110Hz/50% status (inverter->DPC). We emulate it on GPIO5.
 *
 *  WIRING (same ESP harness as single-IGBT build — nothing to resolder):
 *    GPIO4 = PWM command out (push-pull TTL 3.3-5V) -> CN701 pin1 (YELLOW)
 *    GPIO5 = 110Hz/50% feedback emulation, OPEN-DRAIN -> CN701 pin3 (ORANGE)
 *            via 1k series. Board pulls pin3 to 5V; GPIO5 only SINKS/releases,
 *            never sources (pin3 measured 5V HIGH on start = board pull-up).
 *    GND                                            -> CN701 pin2 (BROWN)
 *    GPIO7 = zero-cross in (monitor only)
 *    GPIO1 = Button1 ON/OFF ; GPIO2 = Button2 power step
 *    GPIO6 = capture tap (divider) for 'log' mode
 *
 *  SAFETY: board MUST be chassis-grounded with magnetron load + HV return
 *  intact. Bench-firing unbonded arcs from L701 to the heatsink (observed).
 *  Use a current-limited mains source and watch primary AC current.
 *
 *  CONSOLE (115200):
 *    on / off          start (soft-start then ramp) / stop
 *    pulse <ms>        timed burst (1..30000 ms at run duty, auto-stop).
 *                      Includes soft-start if enabled; counts ms at run duty.
 *                      Safest first-use after rebuild. 'off' aborts early.
 *    p <10..95>        set RUN duty % directly
 *    f <hz>            set command frequency (default 222)
 *    ss <duty> <ms>    set soft-start duty% and hold time (e.g. 'ss 35 2000')
 *    ssoff / sson      disable / enable soft-start (sson = default)
 *    sig2on / sig2off  pin3 110Hz feedback on/off (ON by default here)
 *    log / logdump     capture mode on GPIO6
 *    zc / status / ?
 * ==========================================================================*/

#include "soc/gpio_struct.h"

constexpr int PIN_PWM = 4;     // command out -> CN701 pin1 (220Hz PWM)
constexpr int PIN_SIG2 = 5;    // feedback emulation -> CN701 pin3 (110Hz/50%)
constexpr int PIN_ZC  = 7;     // zero-cross in (monitor only)
constexpr int PIN_BTN_ONOFF = 1;
constexpr int PIN_BTN_POWER = 2;
constexpr int PIN_LOG = 6;     // capture tap (5V via divider)

constexpr bool INVERT_CMD = false;   // flip if higher duty gives LOWER power

constexpr uint32_t TICKS = 200;      // 0.5% duty resolution
uint32_t cmdFreqHz = 222;            // 220Hz command (primary-source confirmed)
int      curDuty   = 50;
bool     running   = false;

// ---- SOFT-START (the key 2-IGBT experiment) ----
// Open at SS_DUTY for SS_MS, then ramp to run power. Mirrors the oven DPC's
// ~2s warm-up at fixed duty before stepping to the requested power level.
bool     softStartEn = true;         // ON by default for the 2-IGBT board
int      ssDuty      = 35;           // warm-up duty % (well below run power)
uint32_t ssMs        = 2000;         // warm-up hold time (ms)
int      ssRampMs    = 800;          // ramp time from ssDuty -> run duty after hold

const uint32_t FREQ_LIST[] = {110, 150, 165, 180, 200, 222, 233, 250, 280, 300};
const int FREQ_COUNT = sizeof(FREQ_LIST)/sizeof(FREQ_LIST[0]);
int freqIdx = 5;                     // 222Hz
// RUN power steps (Button 2). Start conservative for the 2-IGBT board.
const int RUN_STEPS[] = {50, 60, 70, 80, 85, 90};
const int RUN_COUNT = sizeof(RUN_STEPS)/sizeof(RUN_STEPS[0]);
int runIdx = 2;                      // default run power 70% (work up from here)
int curRunDuty = 70;
constexpr int DUTY_MIN = 10, DUTY_MAX = 95;

// ---- ISR PWM + phase-locked half-frequency feedback signal ----
hw_timer_t* pwmTimer = nullptr;
volatile uint32_t tickCounter = 0, sinkTicks = 0;
volatile bool drvEnabled = false;
volatile uint32_t cycleCounter = 0;
volatile bool sig2Enabled = false;

void IRAM_ATTR onPwmTick() {
    if (!drvEnabled) { GPIO.out_w1tc = (1u<<PIN_PWM); GPIO.enable_w1tc=(1u<<PIN_SIG2); return; }
    tickCounter++;
    if (tickCounter>=TICKS){ tickCounter=0; cycleCounter++; }
    bool on = (tickCounter < sinkTicks);
    if (INVERT_CMD) on = !on;
    if (on) GPIO.out_w1ts = (1u<<PIN_PWM);
    else    GPIO.out_w1tc = (1u<<PIN_PWM);
    // PIN_SIG2 = pin3 feedback, OPEN-DRAIN. The board pulls pin3 up to 5V
    // (measured: pin3 goes 5V HIGH on start). So we never SOURCE high - we
    // SINK low for one 220Hz cycle, then RELEASE (high-Z) the next so the
    // board's pull-up restores 5V. Result: 110Hz/50% on a 5V-pulled line.
    // Output latch stays LOW (set once in cmdOn); we only toggle output-ENABLE.
    //   enable bit SET  -> pin drives LOW (sink)      -> "low" half
    //   enable bit CLEAR-> pin high-Z (INPUT)         -> board pull-up = HIGH
    // A 1k series resistor protects the GPIO when high-Z sits at board 5V.
    if (sig2Enabled){
        if (cycleCounter & 1u) GPIO.enable_w1tc = (1u<<PIN_SIG2);  // release -> HIGH
        else                   GPIO.enable_w1ts = (1u<<PIN_SIG2);  // sink   -> LOW
    } else {
        GPIO.enable_w1tc = (1u<<PIN_SIG2);                         // off: high-Z
    }
}

void timerStart(uint32_t fhz) {
    uint32_t f=fhz; if(f<1)f=1; if(f>5000)f=5000;
    uint32_t us=1000000UL/(f*TICKS); if(us<5)us=5;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    if(!pwmTimer){pwmTimer=timerBegin(1000000);timerAttachInterrupt(pwmTimer,&onPwmTick);}
    timerStop(pwmTimer);timerWrite(pwmTimer,0);timerAlarm(pwmTimer,us,true,0);timerStart(pwmTimer);
#else
    if(!pwmTimer){pwmTimer=timerBegin(0,80,true);timerAttachInterrupt(pwmTimer,&onPwmTick,true);}
    timerAlarmWrite(pwmTimer,us,true);timerAlarmEnable(pwmTimer);
#endif
}

// ---- zero-cross capture (monitor only) ----
volatile uint32_t zcLastUs = 0, zcPeriodUs = 0;
volatile bool zcSeen = false;
constexpr uint32_t ZC_LOCKOUT_US = 4000;
void IRAM_ATTR onZeroCross() {
    uint32_t now = micros();
    uint32_t dt = now - zcLastUs;
    if (zcLastUs && dt < ZC_LOCKOUT_US) return;
    if (zcLastUs) zcPeriodUs = dt;
    zcLastUs = now; zcSeen = true;
}

void setDuty(int d){ if(d<DUTY_MIN)d=DUTY_MIN; if(d>DUTY_MAX)d=DUTY_MAX; curDuty=d;
                     sinkTicks=(uint32_t)d*TICKS/100; }

void cmdOff(){ drvEnabled=false; sig2Enabled=false; sinkTicks=0;
    GPIO.out_w1tc=(1u<<PIN_PWM);
    GPIO.enable_w1tc=(1u<<PIN_SIG2); }              // SIG2 high-Z (released)
void cmdOn (){ tickCounter=0; cycleCounter=0; drvEnabled=true; sig2Enabled=true;
    GPIO.out_w1tc=(1u<<PIN_SIG2);                   // hold SIG2 output latch LOW
    timerStart(cmdFreqHz); }                        // ISR toggles only the enable bit

// ---- START with soft-start warm-up then ramp ----
// This is the core 2-IGBT experiment. We bring the command up gently so the
// controller's protection sees a warm-up profile rather than a cold full-duty
// slam (which trips it in ms). Done in loop()-friendly blocking steps; safe
// because nothing else time-critical runs during the ~3s start.
void startRun(){
    cmdFreqHz=FREQ_LIST[freqIdx];
    if(softStartEn){
        Serial.printf("[ON] soft-start: %d%% for %lums, ramp %dms -> %d%% @ %luHz (sig2=%s)\n",
            ssDuty,(unsigned long)ssMs,ssRampMs,curRunDuty,(unsigned long)cmdFreqHz,
            sig2Enabled?"on":"off");
        setDuty(ssDuty);
        cmdOn(); running=true;
        // hold warm-up duty
        delay(ssMs);
        // linear ramp ssDuty -> curRunDuty
        int steps = ssRampMs/20; if(steps<1) steps=1;
        for(int i=1;i<=steps && running;i++){
            int d = ssDuty + (long)(curRunDuty-ssDuty)*i/steps;
            setDuty(d);
            delay(20);
        }
        setDuty(curRunDuty);
        Serial.printf("[ON] ramped to %d%%. BTN2/p to adjust.\n", curRunDuty);
    } else {
        setDuty(curRunDuty);
        cmdOn(); running=true;
        Serial.printf("[ON] direct %d%% @ %luHz (no soft-start). BTN2/p to adjust.\n",
            curRunDuty,(unsigned long)cmdFreqHz);
    }
}
void stopRun(){ cmdOff(); running=false; Serial.println("[OFF] command=0."); }

// Forward decl + serial buffer (declared early so pulseRun() can service serial)
String buf;
void handle(String c);

// ---- TIMED PULSE -----------------------------------------------------------
// Bounded burst for first-use / rebuild bring-up. Soft-start (if enabled)
// runs as normal, THEN holds at run duty for pulseMs, THEN auto-stops.
// Safer than 'on' because forgetting to type 'off' won't cook the IGBTs.
// Max clamped to PULSE_MAX_MS so a typo (e.g. 'pulse 50000') can't run away.
// User can abort early by typing 'off' (handled in handle(); breaks the loop
// because stopRun() clears 'running').
constexpr uint32_t PULSE_MAX_MS = 30000;   // hard 30s ceiling

void pulseRun(uint32_t ms){
    if(ms < 1) ms = 1;
    if(ms > PULSE_MAX_MS){ ms = PULSE_MAX_MS;
        Serial.printf("[PULSE] clamped to max %lums\n",(unsigned long)PULSE_MAX_MS); }
    Serial.printf("[PULSE] %lums at run duty after soft-start. 'off' aborts.\n",
                  (unsigned long)ms);
    startRun();                      // blocks through soft-start + ramp
    if(!running) return;             // startRun aborted (shouldn't, but defensive)
    uint32_t deadline = millis() + ms;
    while(running && (int32_t)(deadline - millis()) > 0){
        // service serial so 'off' can abort; service buttons too
        while(Serial.available()){
            char ch=(char)Serial.read();
            if(ch=='\n'||ch=='\r'){ if(buf.length()){handle(buf);buf="";} }
            else if(buf.length()<32) buf+=ch;
        }
        pollButtons();
        delay(5);
    }
    if(running){
        Serial.printf("[PULSE] %lums elapsed -> auto-stop\n",(unsigned long)ms);
        stopRun();
    } else {
        Serial.println("[PULSE] aborted by user/fault before deadline");
    }
}

// ============================================================================
//  COMMAND-LINE LOGGER (same as single-IGBT build) — capture on GPIO6
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
    Serial.println("---CYCLE BEGIN---");
    Serial.println("cycle,t_start_ms,period_us,high_us,duty_high_pct");
    uint32_t cyc=0,prevRise=0,riseT=0,highUs=0; bool sawRise=false;
    for(uint32_t i=0;i<logIdx;i++){
        if(logL[i]==1){
            if(sawRise){ uint32_t per=logT[i]-prevRise;
                if(per){ Serial.printf("%lu,%.1f,%lu,%lu,%.1f\n",(unsigned long)cyc++,
                    (riseT-base)/1000.0,(unsigned long)per,(unsigned long)highUs,
                    100.0*highUs/per);
                    if((cyc & 0x1F)==0){ Serial.flush(); delay(2); } } }
            prevRise=logT[i]; riseT=logT[i]; sawRise=true;
        } else if(sawRise){ highUs=logT[i]-riseT; }
    }
    Serial.println("---CYCLE END---");
    Serial.flush();
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

void handle(String c){
    c.trim(); if(!c.length()) return;
    String lc=c; lc.toLowerCase();
    if(lc=="?"||lc=="help"){
        Serial.println(F("on off | pulse <ms> | p<10-95> | f<hz> | ss <duty> <ms> | sson ssoff"));
        Serial.println(F("sig2on sig2off (pin3 110Hz) | log logdump | zc status"));
        Serial.println(F("DUAL-IGBT build: soft-start ON, pin3 feedback ON by default"));
        Serial.println(F("'pulse 500' = safest first-use; auto-stops after 500ms at run duty"));
    }
    else if(lc=="on"){ startRun(); }
    else if(lc=="off"){ stopRun(); }
    else if(lc.startsWith("pulse")){
        long ms = c.substring(5).toInt();
        if(ms <= 0){ Serial.println("[PULSE] usage: pulse <ms>  e.g. 'pulse 500'"); }
        else { pulseRun((uint32_t)ms); }
    }
    else if(lc=="sson"){ softStartEn=true; Serial.println("[ss] soft-start ENABLED"); }
    else if(lc=="ssoff"){ softStartEn=false; Serial.println("[ss] soft-start DISABLED (direct drive)"); }
    else if(lc.startsWith("ss ")){
        int sp=c.indexOf(' '); int sp2=c.indexOf(' ',sp+1);
        int d=c.substring(sp+1, sp2>0?sp2:c.length()).toInt();
        if(d>0){ ssDuty=d; if(d<DUTY_MIN)ssDuty=DUTY_MIN; if(d>DUTY_MAX)ssDuty=DUTY_MAX; }
        if(sp2>0){ long m=c.substring(sp2+1).toInt(); if(m>=0) ssMs=m; }
        softStartEn=true;
        Serial.printf("[ss] soft-start %d%% for %lums (ramp %dms)\n",ssDuty,(unsigned long)ssMs,ssRampMs);
    }
    else if(lc=="sig2on"){ sig2Enabled=true; Serial.println("[sig2] pin3 110Hz feedback ENABLED on GPIO5"); }
    else if(lc=="sig2off"){ sig2Enabled=false; GPIO.enable_w1tc=(1u<<PIN_SIG2); Serial.println("[sig2] DISABLED (GPIO5 high-Z / released)"); }
    else if(lc=="log"){ logArm(); }
    else if(lc=="logdump"){ logDump(); }
    else if(lc=="status"){ Serial.printf("[status] run=%d duty=%d%% freq=%luHz ss=%d(%d%%/%lums) sig2=%d invert=%d\n",
                            running,curDuty,(unsigned long)cmdFreqHz,softStartEn,ssDuty,
                            (unsigned long)ssMs,sig2Enabled,INVERT_CMD); }
    else if(lc=="zc"){ Serial.printf("[zc] period=%luus ~%.1fHz (%s)\n",
                        (unsigned long)zcPeriodUs, zcPeriodUs?1000000.0/zcPeriodUs:0.0,
                        zcSeen?"live":"none"); }
    else if(lc.startsWith("p")){ int d=c.substring(1).toInt(); curRunDuty=d; setDuty(d);
                        Serial.printf("[p] run duty=%d%% (clamped 10-95)\n",curDuty); }
    else if(lc.startsWith("f")){ long h=c.substring(1).toInt(); if(h>0){cmdFreqHz=h; if(running)timerStart(h);
                        Serial.printf("[f]%luHz\n",(unsigned long)cmdFreqHz);} }
    else Serial.println("? (try ?)");
}

void setup(){
    Serial.begin(115200);
    pinMode(PIN_PWM, OUTPUT); digitalWrite(PIN_PWM, LOW);
    // PIN_SIG2 open-drain: preload output latch LOW, then leave pin high-Z.
    // ISR sinks by enabling output (drives the latched LOW); releases by
    // disabling output (INPUT -> board 5V pull-up). 1k in series protects pin.
    pinMode(PIN_SIG2, OUTPUT); digitalWrite(PIN_SIG2, LOW);
    GPIO.out_w1tc=(1u<<PIN_SIG2);                  // latch LOW
    GPIO.enable_w1tc=(1u<<PIN_SIG2);               // start high-Z (released)
    GPIO.out_w1tc=(1u<<PIN_PWM); cmdOff();
    pinMode(PIN_ZC, INPUT);
    attachInterrupt(PIN_ZC, onZeroCross, RISING);
    pinMode(PIN_BTN_ONOFF, INPUT_PULLUP);
    pinMode(PIN_BTN_POWER, INPUT_PULLUP);
    cmdFreqHz=FREQ_LIST[freqIdx]; curRunDuty=RUN_STEPS[runIdx]; setDuty(curRunDuty);
    delay(200);
    Serial.println("\n===============================================");
    Serial.println(" Panasonic 2-IGBT inverter driver  (F6645M301GP / M3GP)");
    Serial.println(" EXPERIMENTAL: soft-start + pin3 feedback to beat the ms-abort.");
    Serial.printf ( " Start: %d%% for %lums -> ramp -> %d%% @ %luHz ; pin3 110Hz ON\n",
                    ssDuty,(unsigned long)ssMs,curRunDuty,(unsigned long)cmdFreqHz);
    Serial.println(" GPIO4->pin1(YELLOW) GPIO5->pin3(ORANGE) GND->pin2(BROWN)");
    Serial.println(" 'on' to start, or 'pulse 500' for a 500ms auto-stop burst.");
    Serial.println(" 'ss <duty> <ms>' to tune warm-up.  ? for help.");
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
