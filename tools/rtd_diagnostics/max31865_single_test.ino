// max31865_single_test  —  WIRE-MODE SWEEP  [tag: RTD-SOLO-v2]
// Generic MAX31865 driver (the chip is identical on the purple board and any
// other). ONE board, software SPI. Each second it reads the SAME sensor in
// 2-, 3-, and 4-wire mode and prints all three. Whichever mode shows
// ~109 ohm / ~24 C is the one that matches how YOUR jumpers are soldered —
// no code editing or reflashing to find it.
//
// Wiring (unchanged):
//   VIN -> 3V3 (the VIN pin, not the board's 3V3 pin)   GND -> GND
//   CLK -> GPIO11   SDO -> GPIO13   SDI -> GPIO12   CS -> GPIO1
//   RDY -> unconnected
//
// Board-specific values for YOUR purple PT100 board:
//   RREF 430 (resistor marked 4300/431), RNOMINAL 100. These are NOT Adafruit
//   PT1000 values — they're correct for your board.

#include <Adafruit_MAX31865.h>

#define CS_PIN   1
#define MOSI_PIN 12     // SDI
#define MISO_PIN 13     // SDO
#define SCK_PIN  11     // CLK

#define RREF      430.0
#define RNOMINAL  100.0

Adafruit_MAX31865 rtd(CS_PIN, MOSI_PIN, MISO_PIN, SCK_PIN);

const char* modeName(max31865_numwires_t w){
  return w==MAX31865_2WIRE ? "2-wire" : w==MAX31865_3WIRE ? "3-wire" : "4-wire";
}

void readMode(max31865_numwires_t w){
  rtd.begin(w);
  delay(20);
  uint16_t raw = rtd.readRTD();
  float ohm  = (RREF * raw) / 32768.0;
  float c    = rtd.temperature(RNOMINAL, RREF);
  uint8_t f  = rtd.readFault();
  Serial.printf("  %-6s  raw=%5u  R=%6.2f ohm  T=%7.2f C / %6.1f F  flt=0x%02X%s\n",
                modeName(w), raw, ohm, c, c*9.0/5.0+32.0, f,
                (ohm>95.0 && ohm<160.0) ? "   <== THIS MODE READS CORRECT" : "");
  if(f) rtd.clearFault();
}

void setup(){
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("=== MAX31865 WIRE-MODE SWEEP  [RTD-SOLO-v2] ===");
  Serial.printf("CS=%d CLK=%d SDO=%d SDI=%d  RREF=%.0f (PT100)\n",
                CS_PIN, SCK_PIN, MISO_PIN, MOSI_PIN, RREF);
  Serial.println("Looking for R ~109 ohm (room temp). That mode = your jumper config.");
  Serial.println();
}

void loop(){
  readMode(MAX31865_2WIRE);
  readMode(MAX31865_3WIRE);
  readMode(MAX31865_4WIRE);
  Serial.println();
  delay(1000);
}
