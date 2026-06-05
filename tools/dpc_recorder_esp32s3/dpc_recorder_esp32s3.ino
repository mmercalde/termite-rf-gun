/* ============================================================================
 *  dpc_recorder_esp32s3.ino  —  standalone 2-channel signal recorder
 *  ESP32-S3 Dev Module  ·  Arduino core 2.x/3.x  ·  USB CDC On Boot
 * ----------------------------------------------------------------------------
 *  PURPOSE: passively log the Panasonic DPC <-> inverter signals on CN701,
 *  time-aligned, so we can see the causal relationship between the COMMAND
 *  (DPC -> inverter, pin 3) and the FEEDBACK (inverter -> DPC, pin 1).
 *
 *  This board DRIVES NOTHING. It only listens. Flash on a spare S3.
 *
 *  WIRING (plain resistor dividers — the DPC lines are ~5V, GPIO is 3.3V):
 *    CN701 pin 1 (FEEDBACK) --1k--> GPIO5 --1k--> GND
 *    CN701 pin 3 (COMMAND)  --1k--> GPIO6 --1k--> GND
 *    CN701 pin 2 (COMMON)   ------> this board GND
 *    USB -> laptop  (to capture the CSV:  cat /dev/ttyACM0 > coldstart.txt)
 *
 *  USE:
 *    1. Wire it up, flash, open serial @ 115200.
 *    2. Type 'go' (or press the BOOT button) to ARM.
 *    3. Press the microwave START button.
 *    4. It logs both channels until the window elapses or the buffer fills,
 *       then auto-dumps CSV.  Save the file.  Repeat for hot start.
 *
 *  CSV columns:  idx,t_us,chan,level,dt_us
 *    t_us  = microseconds since the FIRST edge (shared timebase, both channels)
 *    chan  = C (command / pin3)  or  F (feedback / pin1)
 *    level = pin logic level AFTER the edge (0/1)  [note: divided, so 1=~2.5V]
 *    dt_us = microseconds since the PREVIOUS edge on EITHER channel
 * ==========================================================================*/

constexpr int PIN_FB  = 5;    // FEEDBACK  <- CN701 pin 1 (via 1k/1k divider)
constexpr int PIN_CMD = 6;    // COMMAND   <- CN701 pin 3 (via 1k/1k divider)
constexpr int PIN_BTN = 0;    // BOOT button (arm without serial)

// ~30 s of cold-start with retries fits easily. Each edge = 6 bytes.
#define MAXEDGES   30000
#define WINDOW_MS  30000UL
// Real command/feedback edges are never closer than ~565us (narrowest notch at
// 87.5% duty). Anything closer is ringing/noise pickup, not signal. Reject it.
#define GLITCH_US  200UL

volatile uint32_t edgeT[MAXEDGES];   // micros() timestamp
volatile uint8_t  edgeC[MAXEDGES];   // channel: 0=CMD, 1=FB
volatile uint8_t  edgeL[MAXEDGES];   // level after edge
volatile uint32_t idx = 0;
volatile bool     armed = false;
volatile uint32_t firstUs = 0;
volatile uint32_t lastCmdUs = 0, lastFbUs = 0;   // per-channel last-edge time
volatile uint32_t droppedCmd = 0, droppedFb = 0; // glitch-rejected counts

void IRAM_ATTR onCmd(){
    if(!armed || idx>=MAXEDGES) return;
    uint32_t t=micros();
    if(lastCmdUs && (t-lastCmdUs) < GLITCH_US){ droppedCmd++; return; }  // glitch
    lastCmdUs=t;
    if(idx==0) firstUs=t;
    edgeT[idx]=t; edgeC[idx]=0; edgeL[idx]=(uint8_t)digitalRead(PIN_CMD); idx++;
}
void IRAM_ATTR onFb(){
    if(!armed || idx>=MAXEDGES) return;
    uint32_t t=micros();
    if(lastFbUs && (t-lastFbUs) < GLITCH_US){ droppedFb++; return; }      // glitch
    lastFbUs=t;
    if(idx==0) firstUs=t;
    edgeT[idx]=t; edgeC[idx]=1; edgeL[idx]=(uint8_t)digitalRead(PIN_FB); idx++;
}

void arm(){
    idx=0; firstUs=0; lastCmdUs=0; lastFbUs=0; droppedCmd=0; droppedFb=0; armed=true;
    Serial.println("[REC] ARMED — listening on GPIO6(CMD) + GPIO5(FB). Press microwave START.");
}

void dump(){
    armed=false;
    if(!idx){ Serial.println("[REC] no edges captured."); return; }
    uint32_t base=edgeT[0];
    Serial.println("---REC BEGIN---");
    Serial.flush();
    Serial.println("idx,t_us,chan,level,dt_us");
    for(uint32_t i=0;i<idx;i++){
        Serial.printf("%lu,%lu,%c,%u,%lu\n",
            (unsigned long)i,
            (unsigned long)(edgeT[i]-base),
            edgeC[i] ? 'F' : 'C',
            edgeL[i],
            (unsigned long)(i ? edgeT[i]-edgeT[i-1] : 0));
        if((i & 0x1F)==0){ Serial.flush(); delay(2); }   // pace USB-CDC
    }
    Serial.flush();
    // quick summary
    uint32_t cN=0,fN=0;
    for(uint32_t i=0;i<idx;i++){ if(edgeC[i]) fN++; else cN++; }
    Serial.printf("---REC END--- %lu edges  (CMD=%lu  FB=%lu)  span=%lu ms\n",
        (unsigned long)idx,(unsigned long)cN,(unsigned long)fN,
        (unsigned long)((edgeT[idx-1]-base)/1000));
    Serial.printf("    glitches rejected: CMD=%lu FB=%lu (sub-%luus noise)\n",
        (unsigned long)droppedCmd,(unsigned long)droppedFb,(unsigned long)GLITCH_US);
    Serial.println("===CAPTURE DONE===");
    Serial.flush();
}

void setup(){
    Serial.begin(115200);
    pinMode(PIN_CMD, INPUT);
    pinMode(PIN_FB,  INPUT);
    pinMode(PIN_BTN, INPUT_PULLUP);
    attachInterrupt(PIN_CMD, onCmd, CHANGE);
    attachInterrupt(PIN_FB,  onFb,  CHANGE);
    delay(200);
    Serial.println("\n=== DPC 2-channel recorder ===");
    Serial.println(" GPIO6 <- CMD (CN701 pin3)   GPIO5 <- FB (CN701 pin1)   via 1k/1k dividers");
    Serial.println(" type 'go' (or press BOOT) to ARM, then press microwave START.");
    Serial.println(" auto-dumps CSV at 30s or buffer full. 'dump' to dump early. 'go' to re-arm.");
    Serial.println("==============================");
}

String buf;
void handle(String c){
    c.trim(); c.toLowerCase();
    if(c=="go")        arm();
    else if(c=="dump") dump();
    else if(c=="stat") Serial.printf("[REC] armed=%d edges=%lu\n",armed?1:0,(unsigned long)idx);
    else Serial.println("cmds: go | dump | stat");
}

void loop(){
    while(Serial.available()){
        char ch=(char)Serial.read();
        if(ch=='\n'||ch=='\r'){ if(buf.length()){handle(buf);buf="";} }
        else if(buf.length()<16) buf+=ch;
    }
    // BOOT button arms (debounced-ish), so you can run untethered
    static uint32_t lastBtn=0;
    if(digitalRead(PIN_BTN)==LOW && millis()-lastBtn>400){ lastBtn=millis(); if(!armed) arm(); }

    // auto-dump on buffer full or window elapsed
    if(armed && idx>=MAXEDGES){ Serial.println("[REC] buffer full."); dump(); }
    if(armed && idx>0 && (micros()-firstUs) > WINDOW_MS*1000UL){
        Serial.println("[REC] window elapsed."); dump();
    }
    delay(2);
}
