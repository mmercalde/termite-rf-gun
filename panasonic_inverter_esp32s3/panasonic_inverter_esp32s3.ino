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
 *  CONDITIONS TO START (all required):
 *    - frequency MUST be ~220 Hz (other freqs won't start it)
 *    - duty cycle MUST be <= 43% (above 43% it will NOT start)
 *    - TTL signal level 3.3-5V (below 3V it won't work) -> PUSH-PULL, not open-drain
 *    Start low (~20%) then raise duty toward 43% for more power.
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
 *    status / ?
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

constexpr bool INVERT_CMD = false;   // flip if higher duty = lower power on bench

constexpr uint32_t TICKS = 100;
uint32_t cmdFreqHz = 220;            // start guess; Button 2 cycles candidates
int      curDuty   = 30;             // FIXED safe duty during freq hunt (<43%)
bool     running   = false;

// Frequency: confirmed ~220Hz works on the single-IGBT board. Kept selectable.
const uint32_t FREQ_LIST[] = {110, 150, 165, 180, 200, 220, 233, 250, 280, 300};
const int FREQ_COUNT = sizeof(FREQ_LIST)/sizeof(FREQ_LIST[0]);
int freqIdx = 5;                     // 220Hz (confirmed working)
// RUN POWER steps (Button 2). Start gate is <=43%, but RUN power goes higher to
// cross the magnetron oscillation threshold. Ramp up to get actual microwave output.
const int RUN_STEPS[] = {43, 50, 60, 70, 80, 90};
const int RUN_COUNT = sizeof(RUN_STEPS)/sizeof(RUN_STEPS[0]);
int runIdx = 2;                      // default run power 60%
int curRunDuty = 60;
constexpr int HUNT_DUTY = 30;        // (legacy, used if you want a fixed low probe)
constexpr int DUTY_MIN = 10, DUTY_MAX = 90;
constexpr int START_DUTY_MAX = 43;            // startup gate

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

void setDuty(int d){ if(d<DUTY_MIN)d=DUTY_MIN; if(d>DUTY_MAX)d=DUTY_MAX; curDuty=d; sinkTicks=d; }

void cmdOff(){ drvEnabled=false; sig2Enabled=false; sinkTicks=0;
    GPIO.out_w1tc=(1u<<PIN_PWM); GPIO.out_w1tc=(1u<<PIN_SIG2); }   // both LOW
void cmdOn (){ tickCounter=0; cycleCounter=0; drvEnabled=true; sig2Enabled=true; timerStart(cmdFreqHz); }

// Start at safe <=43% gate, then RAMP UP past 43% toward full power so the
// magnetron actually crosses its oscillation threshold (~4kV anode + hot filament).
// Staying at the startup gate gives HV but NO oscillation - must drive harder.
void startRun(){
    setDuty(START_DUTY_MAX); cmdFreqHz=FREQ_LIST[freqIdx]; cmdOn(); running=true;
    Serial.printf("[ON] start %d%% @ %luHz. Filament warming...\n",
                  START_DUTY_MAX,(unsigned long)cmdFreqHz);
    delay(2000);                         // let filament heat + magnetron begin
    setDuty(curRunDuty);
    Serial.printf("[ON] ramped to %d%% (drive for oscillation). Use BTN2/p to adjust.\n", curRunDuty);
}
void stopRun(){ cmdOff(); running=false; Serial.println("[OFF] command=0."); }

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
        Serial.println(F("BTN1=on/off BTN2=cycle-freq | GPIO4=pin1 GPIO5=pin3 ; duty fixed 30%"));
    }
    else if(lc=="sig2on"){ sig2Enabled=true; Serial.println("[sig2] 110Hz signal ENABLED on GPIO5"); }
    else if(lc=="sig2off"){ sig2Enabled=false; GPIO.out_w1tc=(1u<<PIN_SIG2); Serial.println("[sig2] DISABLED (GPIO5 held low)"); }
    else if(lc=="on"){ startRun(); }
    else if(lc=="off"){ stopRun(); }
    else if(lc=="status"){ Serial.printf("[status] running=%d power=%d%% freq=%luHz zcPeriod=%luus invert=%d\n",
                            running,curDuty,(unsigned long)cmdFreqHz,(unsigned long)zcPeriodUs,INVERT_CMD); }
    else if(lc=="zc"){ Serial.printf("[zc] last period=%luus (%s)  ~%.1fHz\n",
                        (unsigned long)zcPeriodUs, zcSeen?"live":"none yet",
                        zcPeriodUs?1000000.0/zcPeriodUs:0.0); }
    else if(lc.startsWith("p")){ int d=c.substring(1).toInt(); setDuty(d);
                        Serial.printf("[p] power=%d%% (clamped 10-90)\n",curDuty); }
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
    setDuty(START_DUTY_MAX); cmdFreqHz=FREQ_LIST[freqIdx]; curRunDuty=RUN_STEPS[runIdx];
    delay(200);
    Serial.println("\n===============================================");
    Serial.println(" Panasonic CN701 3-pin inverter driver");
    Serial.println(" TTL push-pull. Single-IGBT board. Start <=43% then ramp BTN2 for oscillation.");
    Serial.println(" BTN1(GPIO1)=on/off  BTN2(GPIO2)=RUN POWER (ramp to oscillate)");
    Serial.println(" GPIO4->YELLOW(PWM) ; BROWN->GND ; ORANGE->1k->GND ; zc on GPIO7");
    Serial.println(" ? for help.");
    Serial.println("===============================================");
}

void loop(){
    while(Serial.available()){
        char ch=(char)Serial.read();
        if(ch=='\n'||ch=='\r'){ if(buf.length()){handle(buf);buf="";} }
        else if(buf.length()<32) buf+=ch;
    }
    pollButtons();
    delay(2);
}
