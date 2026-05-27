/* ============================================================================
 *  lg_inverter_esp32s3.ino  —  LG inverter driver (same pattern as Panasonic)
 *  ESP32-S3 Dev Module · core 2.x/3.x · 4MB · PSRAM off · USB CDC On Boot
 * ----------------------------------------------------------------------------
 *  TARGET: LG inverter board EBR82899411 / EBR82899402 (MSER2090S NeoChef,
 *          120 VAC / 1200 W, magnetron 2M286-21).
 *
 *  SAME ARCHITECTURE AS THE PANASONIC (proven on this bench):
 *    - A COMMAND opto you PWM (power level = duty)
 *    - An ENABLE opto the main board drives HIGH on START to run the inverter
 *      (scoped: the enable line goes 5V HIGH when start is pressed)
 *    - Controller is cold until MAINS is applied (same as Panasonic — the oven's
 *      relay gates mains to the inverter; here, apply mains to wake it)
 *
 *  So we drive it exactly like the working Panasonic single-IGBT board:
 *    PWM the command opto, assert the enable opto HIGH = run.
 *
 *  BOTH lines are board-pulled to 5V (open-collector style, scoped HIGH).
 *  The ESP therefore drives them OPEN-DRAIN: sink to pull low, release to let
 *  the board's pull-up take them to 5V. NEVER sources into the 5V line.
 *  A 1k series resistor protects the (non-5V-tolerant) GPIO on each line.
 *
 *  WIRING (LG CN3 — confirmed bench pinout):
 *    GPIO4 -> 1k -> CN3 command line   COMMAND PWM (open-drain, board pulls 5V)
 *    GPIO5 -> 1k -> CN3 enable line    ENABLE (open-drain; assert = run)
 *    GND   -> CN3 pin6 (GND)
 *    (CN3 pin7 = +15V board supply — board powered from mains, not from ESP)
 *  NOTE: confirm which physical CN3 pin is COMMAND vs ENABLE before powering;
 *        the command opto LED is fed via on-board 330R on pins 3/4, the enable
 *        line is the one scoped going 5V HIGH on start.
 *
 *  SAFETY: 120 V board — apply mains to wake the controller (it stays dead on
 *  logic bias alone, same as Panasonic). Hot side is at mains potential.
 *  Caps discharged between attempts. Magnetron loaded, board grounded.
 *  NEVER fire at the wrong voltage and never repeatedly re-press into a board
 *  that's arcing/discharging rather than oscillating (that is what destroys
 *  IGBTs — see project README failure analysis).
 *
 *  CONSOLE (115200):
 *    on / off          enable HIGH + PWM the command / stop (both released)
 *    p <10..95>        set command duty %
 *    f <hz>            set command frequency (default 222, sweepable)
 *    en / endis        enable line assert / release (test enable independently)
 *    pol               flip command drive polarity (sink-low-on vs release-on)
 *    enpol             flip enable assert polarity (sink=assert vs release=assert)
 *    zc / status / ?
 *
 *  Start point mirrors the Panasonic that worked: 222 Hz, sweep duty, no
 *  soft-start. If it won't run, sweep f and duty, then try the polarity flips.
 * ==========================================================================*/

#include "soc/gpio_struct.h"

constexpr int PIN_CMD = 4;     // command PWM -> CN3 command line (via 1k)
constexpr int PIN_EN  = 5;     // enable      -> CN3 enable line  (via 1k)
constexpr int PIN_ZC  = 7;     // zero-cross in (monitor only)
constexpr int PIN_BTN_ONOFF = 1;
constexpr int PIN_BTN_POWER = 2;

// Both lines are board-pulled to 5V (open-collector). "Assert" = sink LOW by
// default (matches the command opto: pin held high by board, sink to signal).
// If a line needs the opposite sense, flip its polarity flag at runtime.
bool cmdSinkOn = true;   // true: command "on" half = sink LOW (LED conducts)
bool enSinkAssert = false; // enable: scoped goes HIGH on start -> assert = RELEASE (let board 5V) ; flip if needed

constexpr uint32_t TICKS = 200;       // 0.5% duty resolution
uint32_t cmdFreqHz = 222;             // start where the Panasonic worked
int      curDuty   = 50;
bool     running   = false;

const uint32_t FREQ_LIST[] = {110, 150, 165, 180, 200, 222, 233, 250, 280, 300};
const int FREQ_COUNT = sizeof(FREQ_LIST)/sizeof(FREQ_LIST[0]);
int freqIdx = 5;                      // 222Hz
const int DUTY_STEPS[] = {30, 40, 50, 60, 70, 80, 85};
const int DUTY_COUNT = sizeof(DUTY_STEPS)/sizeof(DUTY_STEPS[0]);
int dutyIdx = 4;                      // 70%
constexpr int DUTY_MIN = 10, DUTY_MAX = 95;

hw_timer_t* pwmTimer = nullptr;
volatile uint32_t tickCounter = 0, sinkTicks = 0;
volatile bool drvEnabled = false;

// Open-drain helpers: ENABLE output = sink (drive latched LOW); DISABLE = high-Z.
// Output latch for both pins is held LOW; we toggle only the output-ENABLE bit.
inline void odSink(int pin){ GPIO.enable_w1ts = (1u<<pin); }   // drive LOW
inline void odRelease(int pin){ GPIO.enable_w1tc = (1u<<pin); } // high-Z -> board 5V

void IRAM_ATTR onPwmTick() {
    if (!drvEnabled) { odRelease(PIN_CMD); return; }
    tickCounter++;
    if (tickCounter>=TICKS) tickCounter=0;
    bool onHalf = (tickCounter < sinkTicks);   // "on" portion of the duty
    // command line: on-half sinks LOW (LED conducts) if cmdSinkOn, else releases
    bool sinkNow = cmdSinkOn ? onHalf : !onHalf;
    if (sinkNow) odSink(PIN_CMD); else odRelease(PIN_CMD);
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

volatile uint32_t zcLastUs=0, zcPeriodUs=0; volatile bool zcSeen=false;
constexpr uint32_t ZC_LOCKOUT_US=4000;
void IRAM_ATTR onZeroCross(){ uint32_t now=micros(); uint32_t dt=now-zcLastUs;
    if(zcLastUs && dt<ZC_LOCKOUT_US) return; if(zcLastUs) zcPeriodUs=dt;
    zcLastUs=now; zcSeen=true; }

void setDuty(int d){ if(d<DUTY_MIN)d=DUTY_MIN; if(d>DUTY_MAX)d=DUTY_MAX; curDuty=d;
                     sinkTicks=(uint32_t)d*TICKS/100; }

// ENABLE line: assert = "run". Per scope, board pulls it HIGH on start, so
// asserting "run" = let it sit HIGH = RELEASE (high-Z). If the board instead
// wants it actively pulled, flip enSinkAssert and assert becomes SINK.
void enableAssert(){ if(enSinkAssert) odSink(PIN_EN); else odRelease(PIN_EN); }
void enableRelease(){ if(enSinkAssert) odRelease(PIN_EN); else odSink(PIN_EN); }

void startRun(){
    cmdFreqHz=FREQ_LIST[freqIdx]; setDuty(DUTY_STEPS[dutyIdx]);
    enableAssert();                 // assert ENABLE = run (like DPC on start)
    tickCounter=0; drvEnabled=true; timerStart(cmdFreqHz);
    running=true;
    Serial.printf("[ON] EN asserted, PWM %d%% @ %luHz (cmdSinkOn=%d enSinkAssert=%d)\n",
        curDuty,(unsigned long)cmdFreqHz,cmdSinkOn,enSinkAssert);
}
void stopRun(){ drvEnabled=false; odRelease(PIN_CMD); enableRelease();
    running=false; Serial.println("[OFF] command released, enable released."); }

unsigned long lastB1=0,lastB2=0; constexpr unsigned long DEBOUNCE_MS=200;
int prevB1=HIGH, prevB2=HIGH;
void btnOnOff(){ if(running) stopRun(); else startRun(); }
void btnPower(){ dutyIdx=(dutyIdx+1)%DUTY_COUNT; if(running) setDuty(DUTY_STEPS[dutyIdx]);
    Serial.printf("[BTN2] duty -> %d%%%s\n", DUTY_STEPS[dutyIdx], running?" (live)":""); }
void pollButtons(){ int b1=digitalRead(PIN_BTN_ONOFF), b2=digitalRead(PIN_BTN_POWER);
    unsigned long now=millis();
    if(prevB1==HIGH&&b1==LOW&&now-lastB1>DEBOUNCE_MS){lastB1=now;btnOnOff();}
    if(prevB2==HIGH&&b2==LOW&&now-lastB2>DEBOUNCE_MS){lastB2=now;btnPower();}
    prevB1=b1; prevB2=b2; }

String buf;
void handle(String c){ c.trim(); if(!c.length())return; String lc=c; lc.toLowerCase();
    if(lc=="?"||lc=="help"){
        Serial.println(F("on off | p<10-95> | f<hz> | en endis | pol | enpol | zc status"));
        Serial.println(F("LG build: PWM command opto + assert enable opto (= Panasonic pattern)"));
    }
    else if(lc=="on"){ startRun(); }
    else if(lc=="off"){ stopRun(); }
    else if(lc=="en"){ enableAssert(); Serial.println("[en] ENABLE asserted"); }
    else if(lc=="endis"){ enableRelease(); Serial.println("[en] ENABLE released"); }
    else if(lc=="pol"){ cmdSinkOn=!cmdSinkOn; Serial.printf("[pol] command sink-on=%d\n",cmdSinkOn); }
    else if(lc=="enpol"){ enSinkAssert=!enSinkAssert; if(running)enableAssert();
        Serial.printf("[enpol] enable sink-assert=%d\n",enSinkAssert); }
    else if(lc=="status"){ Serial.printf("[status] run=%d duty=%d%% f=%luHz cmdSinkOn=%d enSinkAssert=%d\n",
        running,curDuty,(unsigned long)cmdFreqHz,cmdSinkOn,enSinkAssert); }
    else if(lc=="zc"){ Serial.printf("[zc] %luus ~%.1fHz (%s)\n",(unsigned long)zcPeriodUs,
        zcPeriodUs?1000000.0/zcPeriodUs:0.0, zcSeen?"live":"none"); }
    else if(lc.startsWith("p")){ int d=c.substring(1).toInt(); setDuty(d);
        Serial.printf("[p] duty=%d%%\n",curDuty); }
    else if(lc.startsWith("f")){ long h=c.substring(1).toInt(); if(h>0){cmdFreqHz=h;
        if(running)timerStart(h); Serial.printf("[f]%luHz\n",(unsigned long)cmdFreqHz);} }
    else Serial.println("? (try ?)");
}

void setup(){
    Serial.begin(115200);
    // Both lines open-drain: latch LOW, start released (high-Z -> board 5V).
    pinMode(PIN_CMD, OUTPUT); digitalWrite(PIN_CMD, LOW);
    GPIO.out_w1tc=(1u<<PIN_CMD); odRelease(PIN_CMD);
    pinMode(PIN_EN, OUTPUT); digitalWrite(PIN_EN, LOW);
    GPIO.out_w1tc=(1u<<PIN_EN); odRelease(PIN_EN);
    pinMode(PIN_ZC, INPUT); attachInterrupt(PIN_ZC, onZeroCross, RISING);
    pinMode(PIN_BTN_ONOFF, INPUT_PULLUP); pinMode(PIN_BTN_POWER, INPUT_PULLUP);
    cmdFreqHz=FREQ_LIST[freqIdx]; setDuty(DUTY_STEPS[dutyIdx]);
    delay(200);
    Serial.println("\n===============================================");
    Serial.println(" LG inverter driver (EBR82899411/402) - Panasonic-pattern");
    Serial.println(" PWM command opto + assert enable opto. Apply MAINS to wake.");
    Serial.printf ( " Start: PWM %d%% @ %luHz, enable asserted on 'on'.\n",
                    DUTY_STEPS[dutyIdx],(unsigned long)cmdFreqHz);
    Serial.println(" GPIO4->1k->CN3 command | GPIO5->1k->CN3 enable | GND->CN3 pin6");
    Serial.println(" 'on' to start. f/p to sweep. pol/enpol if it won't run. ? help.");
    Serial.println("===============================================");
}

void loop(){
    while(Serial.available()){ char ch=(char)Serial.read();
        if(ch=='\n'||ch=='\r'){ if(buf.length()){handle(buf);buf="";} }
        else if(buf.length()<32) buf+=ch; }
    pollButtons();
    delay(2);
}
