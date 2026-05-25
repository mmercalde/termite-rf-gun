/* ============================================================================
 *  panasonic_inverter_esp32s3.ino   —  Panasonic CN701 3-pin inverter driver
 *  ESP32-S3 Dev Module · core 2.x/3.x · 4MB · PSRAM off · USB CDC On Boot
 * ----------------------------------------------------------------------------
 *  SAME ESP32 WIRING as the LG build — nothing to desolder. Only the command
 *  wire (GPIO4) lands on Panasonic CN701 pin 3 instead of LG CN3 pin 3, and the
 *  zero-cross detector's line tap is moved to the Panasonic L/N. Drives the
 *  Panasonic dumb 3-pin interface: ~233 Hz PWM command, valid duty 25-75%.
 *  No startup sequence, no zero-cross gating needed (Panasonic ramps itself).
 *  The zc monitor is kept live for reference + carry-over to the LG board.
 *
 *  PANASONIC 3-wire control (per documented standalone operation):
 *    YELLOW = PWM command in   <- ESP GPIO4 (true TTL push-pull, 3.3-5V, 220Hz)
 *    ORANGE = must be tied to GND through a 1k resistor (can't float)
 *    BROWN  = ground            -> ESP GND
 *  CONDITIONS TO START (per CAPTURE run1 of the working oven's command):
 *    - frequency = 222 Hz (measured period 4509us)
 *    - steady-state command = ~85% HIGH duty (idle HIGH, ~665us LOW notch, run2)
 *    - NO soft-start ramp: oven drives full duty from cycle one; inverter's own
 *      closed-loop current regulation handles magnetron inrush internally
 *    - more HIGH duty = more power (oven's only down-modulation dipped to ~65%)
 *    - TTL 3.3-5V push-pull (below 3V it won't work)
 *    Drive 85% directly. The old "<=43% gate" just under-drove: HV but no osc.
 *
 *  ESP32-S3 PINS (unchanged hardware from LG build):
 *    GPIO4 = PWM command out (push-pull TTL) -> Panasonic YELLOW
 *    GPIO7 = zero-cross in    <- SFH617A detector (monitor only; zc still works)
 *    GPIO1 = Button 1  (ON/OFF toggle)
 *    GPIO2 = Button 2  (power level step, 15-43%)
 *    GND -> Panasonic BROWN ; ORANGE -> 1k -> GND
 *
 *  CONSOLE (115200):
 *    on / off        start / stop the command
 *    p <25..75>      set power (duty %) directly, clamped to valid range
 *    f <hz>          set command frequency (default 233)
 *    zc              report measured zero-cross period (monitor only)
 *    log             CAPTURE MODE: log the real oven's command on GPIO6, then
 *                    cold-start the oven; dumps RAW + per-cycle CSV over serial
 *    logdump         dump the capture early (before the buffer fills)
 *    status / ?
 *
 *  CAPTURE WIRING (for 'log' mode — tapping the working oven's main board):
 *    oven CN701 pin1 (5V PWM) --[2.2k]--+--[3.3k]-- GND ;  tap -> GPIO6 (~3.0V)
 *    oven GND -- ESP GND (common ground required). GPIO6 is NOT 5V tolerant.
 *
 *  BUTTONS:
 *    BTN1 (GPIO1) = ON/OFF toggle
 *    BTN2 (GPIO2) = step power level 25->35->45->55->65->75 (wraps)
 *
 *  COMMAND POLARITY NOTE: GPIO4 uses the same software open-drain sink scheme as
 *  the LG build (sink = opto LED on). If on the bench higher duty gives LOWER
 *  power (inverted), flip INVERT_CMD below to true and re-flash. One-line change.
 * ==========================================================================*/

#include "soc/gpio_struct.h"

constexpr int PIN_PWM = 4;     // command out -> CN701 pin1 (220Hz PWM)
constexpr int PIN_SIG2 = 5;    // second signal -> CN701 pin3 (110Hz/50%, rectangular variant)
constexpr int PIN_ZC  = 7;     // zero-cross in (monitor only for Panasonic)
constexpr int PIN_BTN_ONOFF = 1;
constexpr int PIN_BTN_POWER = 2;
constexpr int PIN_LOG = 6;     // command-line capture tap (5V via divider) -> see logger

constexpr bool INVERT_CMD = false;   // CONFIRMED: more HIGH duty = more power (capture run1)

constexpr uint32_t TICKS = 200;      // 0.5% duty resolution
uint32_t cmdFreqHz = 222;            // MEASURED oven command period 4509us = 222Hz
int      curDuty   = 85;             // PERCENT high-duty (MEASURED run2: 85.0-85.5%)
bool     running   = false;

// Frequency: MEASURED 222Hz on the working oven (4509us period). Kept selectable.
const uint32_t FREQ_LIST[] = {110, 150, 165, 180, 200, 222, 233, 250, 280, 300};
const int FREQ_COUNT = sizeof(FREQ_LIST)/sizeof(FREQ_LIST[0]);
int freqIdx = 5;                     // 222Hz (measured)
// RUN POWER steps (Button 2), in PERCENT high-duty. Oven command = ~85%.
// Its only down-modulation in the capture dipped to ~65% (less power), confirming
// higher HIGH-duty = more power. ~85% is the measured oven command.
const int RUN_STEPS[] = {65, 75, 80, 85, 88, 90};   // % HIGH duty
const int RUN_COUNT = sizeof(RUN_STEPS)/sizeof(RUN_STEPS[0]);
int runIdx = 3;                      // default run power 85% (== measured oven command)
int curRunDuty = 85;
constexpr int HUNT_DUTY = 30;        // (legacy)
constexpr int DUTY_MIN = 10, DUTY_MAX = 95;
constexpr int FULL_DUTY = 85;        // MEASURED oven command high-duty (run2: 85.0-85.5%)

// ---- ISR PWM + phase-locked half-frequency second signal ----
hw_timer_t* pwmTimer = nullptr;
volatile uint32_t tickCounter = 0, sinkTicks = 0;
volatile bool drvEnabled = false;
// Second signal (PIN_SIG2): 110Hz @ 50%, phase-locked to the 220Hz on PIN_PWM.
// The ISR runs at cmdFreqHz*TICKS. One full PIN_PWM cycle = TICKS ticks.
// 110Hz = half of 220Hz, so PIN_SIG2 toggles its half-period every TICKS ticks
// (i.e. one PIN_PWM period high, next period low) -> 50% duty at half frequency.
volatile uint32_t cycleCounter = 0;
volatile bool sig2Enabled = false;

void IRAM_ATTR onPwmTick() {
    if (!drvEnabled) { GPIO.out_w1tc = (1u<<PIN_PWM); GPIO.out_w1tc=(1u<<PIN_SIG2); return; }
    tickCounter++;
    if (tickCounter>=TICKS){
        tickCounter=0;
        cycleCounter++;                       // one full PIN_PWM (220Hz) cycle elapsed
    }
    // main 220Hz command on PIN_PWM
    bool on = (tickCounter < sinkTicks);
    if (INVERT_CMD) on = !on;
    if (on) GPIO.out_w1ts = (1u<<PIN_PWM);
    else    GPIO.out_w1tc = (1u<<PIN_PWM);
    // 110Hz/50% on PIN_SIG2: high for one 220Hz cycle, low for the next
    if (sig2Enabled){
        if (cycleCounter & 1u) GPIO.out_w1ts = (1u<<PIN_SIG2);
        else                   GPIO.out_w1tc = (1u<<PIN_SIG2);
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

// ---- zero-cross capture (monitor only; kept for carry-over to LG) ----
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

// d is PERCENT high-duty; scale to ticks (TICKS=200 -> 0.5% resolution)
void setDuty(int d){ if(d<DUTY_MIN)d=DUTY_MIN; if(d>DUTY_MAX)d=DUTY_MAX; curDuty=d;
                     sinkTicks=(uint32_t)d*TICKS/100; }

void cmdOff(){ drvEnabled=false; sig2Enabled=false; sinkTicks=0;
    GPIO.out_w1tc=(1u<<PIN_PWM); GPIO.out_w1tc=(1u<<PIN_SIG2); }   // both LOW
void cmdOn (){ tickCounter=0; cycleCounter=0; drvEnabled=true; sig2Enabled=true; timerStart(cmdFreqHz); }

// CAPTURE FINDING (run2, clean): oven sends ~85% HIGH duty @ 222Hz from cycle one
// from the very first cycle - there is NO soft-start staircase. The inverter's
// own closed-loop current regulation handles magnetron inrush internally; the
// command is simply "full power". So we drive the measured command directly.
// (The old 43% gate just under-drove the tube: HV present, no oscillation.)
void startRun(){
    cmdFreqHz=FREQ_LIST[freqIdx];
    setDuty(curRunDuty);                 // default 85% == measured oven command
    cmdOn(); running=true;
    Serial.printf("[ON] driving %d%% HIGH @ %luHz (measured oven command). BTN2/p to adjust.\n",
                  curRunDuty,(unsigned long)cmdFreqHz);
}
void stopRun(){ cmdOff(); running=false; Serial.println("[OFF] command=0."); }

// ============================================================================
//  COMMAND-LINE LOGGER — capture the real oven's command on CN701 pin1.
//  Edge-logs every transition with microsecond timestamps (no buffer-depth
//  tradeoff like the DS213), then prints RAW + per-cycle duty CSV. This is the
//  measurement that tells the drive code the true ramp + steady-state shape.
//  Never drives and logs at once (logArm stops any running command first).
// ============================================================================
#define LOG_MAXEDGES 8000            // ~18 s at ~440 edges/s. 40KB SRAM.

// ---- BOOT AUTO-LOG: unattended capture so the PC side is just a `cat` ----
// When 1: the board arms the logger on boot, captures from the first edge, and
// auto-dumps after LOG_WINDOW_MS (or buffer full). No typing required, so the
// entire PC capture is:   cat /dev/ttyACM0 > run.txt     (Ctrl-C after "DONE").
// This build does NOT drive the inverter — set BOOT_AUTOLOG 0 to return to
// normal drive mode (one-line change, re-flash).
#define BOOT_AUTOLOG 0
constexpr uint32_t LOG_WINDOW_MS = 10000;   // capture window measured from first edge

volatile uint32_t logT[LOG_MAXEDGES];
volatile uint8_t  logL[LOG_MAXEDGES];
volatile uint32_t logIdx = 0;
volatile bool     logArmed = false;
volatile uint32_t logFirstUs = 0;           // micros() of the very first edge

void IRAM_ATTR onLogEdge(){
    if(!logArmed || logIdx>=LOG_MAXEDGES) return;
    uint32_t t=micros();
    if(logIdx==0) logFirstUs=t;             // mark window start at first real edge
    logT[logIdx]=t;
    logL[logIdx]=(uint8_t)digitalRead(PIN_LOG);   // new level after the edge
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
        if((i & 0x1F)==0){ Serial.flush(); delay(2); }   // pace every 32 lines -> no overrun
    }
    Serial.flush();
    Serial.printf("---RAW END--- %lu edges\n",(unsigned long)logIdx);
    // per-cycle: period = rise-to-rise, high = rise-to-fall (command idles HIGH,
    // notches LOW, so duty_high = idle fraction; watch which way it moves w/ power)
    Serial.println("---CYCLE BEGIN---");
    Serial.println("cycle,t_start_ms,period_us,high_us,duty_high_pct");
    uint32_t cyc=0,prevRise=0,riseT=0,highUs=0; bool sawRise=false;
    for(uint32_t i=0;i<logIdx;i++){
        if(logL[i]==1){                       // rising
            if(sawRise){ uint32_t per=logT[i]-prevRise;
                if(per){ Serial.printf("%lu,%.1f,%lu,%lu,%.1f\n",(unsigned long)cyc++,
                    (riseT-base)/1000.0,(unsigned long)per,(unsigned long)highUs,
                    100.0*highUs/per);
                    if((cyc & 0x1F)==0){ Serial.flush(); delay(2); } } }
            prevRise=logT[i]; riseT=logT[i]; sawRise=true;
        } else if(sawRise){ highUs=logT[i]-riseT; }   // falling
    }
    Serial.println("---CYCLE END---");
    Serial.flush();
    Serial.println("===CAPTURE DONE=== (safe to Ctrl-C the cat now)");
    Serial.flush();
}

void logArm(){
    if(running) stopRun();                    // never drive and log simultaneously
    logIdx=0; logFirstUs=0; logArmed=true;
    pinMode(PIN_LOG, INPUT);
    attachInterrupt(PIN_LOG, onLogEdge, CHANGE);
    Serial.println("[log] ARMED on GPIO6 — cold-start the oven now. 'logdump' to dump early.");
}

// ---- buttons ----
unsigned long lastB1=0, lastB2=0;
constexpr unsigned long DEBOUNCE_MS=200;
int prevB1=HIGH, prevB2=HIGH;

void btnOnOff(){ if(running) stopRun(); else startRun(); }
// Button 2 = RUN POWER level (ramp up to reach/sustain oscillation).
void btnPower(){
    runIdx=(runIdx+1)%RUN_COUNT;
    curRunDuty=RUN_STEPS[runIdx];
    if(running){ setDuty(curRunDuty); }   // live power change
    Serial.printf("[BTN2] power -> %d%%%s\n", curRunDuty, running?" (live)":" (set; BTN1 to run)");
}
void pollButtons(){
    int b1=digitalRead(PIN_BTN_ONOFF), b2=digitalRead(PIN_BTN_POWER);
    unsigned long now=millis();
    if(prevB1==HIGH && b1==LOW && now-lastB1>DEBOUNCE_MS){ lastB1=now; btnOnOff(); }
    if(prevB2==HIGH && b2==LOW && now-lastB2>DEBOUNCE_MS){ lastB2=now; btnPower(); }
    prevB1=b1; prevB2=b2;
}

String buf;
void handle(String c){
    c.trim(); if(!c.length()) return;
    String lc=c; lc.toLowerCase();
    if(lc=="?"||lc=="help"){
        Serial.println(F("on  off  p<10-90>  f<hz>  sig2on  sig2off  zc  status"));
        Serial.println(F("log  logdump  (capture oven command on GPIO6 via divider)"));
        Serial.println(F("BTN1=on/off BTN2=cycle-freq | GPIO4=pin1 GPIO5=pin3 ; duty fixed 30%"));
    }
    else if(lc=="sig2on"){ sig2Enabled=true; Serial.println("[sig2] 110Hz signal ENABLED on GPIO5"); }
    else if(lc=="sig2off"){ sig2Enabled=false; GPIO.out_w1tc=(1u<<PIN_SIG2); Serial.println("[sig2] DISABLED (GPIO5 held low)"); }
    else if(lc=="on"){ startRun(); }
    else if(lc=="off"){ stopRun(); }
    else if(lc=="log"){ logArm(); }
    else if(lc=="logdump"){ logDump(); }
    else if(lc=="status"){ Serial.printf("[status] running=%d power=%d%% freq=%luHz zcPeriod=%luus invert=%d\n",
                            running,curDuty,(unsigned long)cmdFreqHz,(unsigned long)zcPeriodUs,INVERT_CMD); }
    else if(lc=="zc"){ Serial.printf("[zc] last period=%luus (%s)  ~%.1fHz\n",
                        (unsigned long)zcPeriodUs, zcSeen?"live":"none yet",
                        zcPeriodUs?1000000.0/zcPeriodUs:0.0); }
    else if(lc.startsWith("p")){ int d=c.substring(1).toInt(); setDuty(d);
                        Serial.printf("[p] power=%d%% (clamped 10-95)\n",curDuty); }
    else if(lc.startsWith("f")){ long h=c.substring(1).toInt(); if(h>0){cmdFreqHz=h; if(running)timerStart(h);
                        Serial.printf("[f]%luHz\n",(unsigned long)cmdFreqHz);} }
    else Serial.println("? (try ?)");
}

void setup(){
    Serial.begin(115200);
    pinMode(PIN_PWM, OUTPUT); digitalWrite(PIN_PWM, LOW);
    pinMode(PIN_SIG2, OUTPUT); digitalWrite(PIN_SIG2, LOW);
    GPIO.out_w1tc=(1u<<PIN_PWM); GPIO.out_w1tc=(1u<<PIN_SIG2); cmdOff();
    pinMode(PIN_ZC, INPUT);
    attachInterrupt(PIN_ZC, onZeroCross, RISING);
    pinMode(PIN_BTN_ONOFF, INPUT_PULLUP);
    pinMode(PIN_BTN_POWER, INPUT_PULLUP);
    cmdFreqHz=FREQ_LIST[freqIdx]; curRunDuty=RUN_STEPS[runIdx]; setDuty(curRunDuty);
    delay(200);
    Serial.println("\n===============================================");
    Serial.println(" Panasonic CN701 3-pin inverter driver");
    Serial.println(" MEASURED command: 222Hz, ~85% HIGH duty, no soft-start (drive direct).");
    Serial.println(" BTN1(GPIO1)=on/off  BTN2(GPIO2)=RUN POWER (default 85%)");
    Serial.println(" GPIO4->YELLOW(PWM) ; BROWN->GND ; ORANGE->1k->GND ; zc on GPIO7");
    Serial.println(" 'log' = capture oven command on GPIO6.  ? for help.");
    Serial.println("===============================================");
#if BOOT_AUTOLOG
    delay(500);
    Serial.println("[AUTOLOG] capture build. PC side: cat /dev/ttyACM0 > run.txt");
    Serial.println("[AUTOLOG] arming now — cold-start the oven on Quick Min.");
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
        Serial.println("[log] capture window elapsed."); logDump();
    }
    delay(2);
}
