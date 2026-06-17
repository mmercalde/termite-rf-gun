// max31865_manualcs_test  —  MANUAL CS + POLARITY-LOCK GAP  [tag: RTD-MANCS-v1]
// Implements the documented fix: the MAX31865 detects SPI clock polarity when
// CS first goes low, and needs a GAP between CS-low and the first clock edge.
// The ESP32 SPI driver drops CS and clock together (no gap) -> the chip never
// locks SPI mode -> garbage / 0xFF / rail values. Here we take CS OFF the SPI
// peripheral, drive it as a plain GPIO, and insert an explicit gap. No Adafruit
// library — bare register access so nothing hides the CS timing.
//
// Wiring (UNCHANGED):
//   VIN -> 3.3V (clean)   GND -> GND (common)
//   CLK -> GPIO11   SDO -> GPIO13   SDI -> GPIO12   CS -> GPIO1   RDY -> n/c
//
// PT100 board: RREF 430. Set WIRE3 to match your jumpers (true = 3-wire).

#include <SPI.h>

#define CS_PIN   1
#define SCK_PIN  11     // CLK
#define MISO_PIN 13     // SDO
#define MOSI_PIN 12     // SDI

#define RREF     430.0
#define RNOMINAL 100.0
#define WIRE3    true   // true = 3-wire jumpers, false = 2/4-wire
#define CS_GAP_US 10    // CS-low -> first-clock gap (the fix). bump if needed.

SPIClass SPIRTD(FSPI);
SPISettings cfg(1000000, MSBFIRST, SPI_MODE1);   // MAX31865 = SPI mode 1, 1 MHz

uint8_t rd8(uint8_t addr){
  SPIRTD.beginTransaction(cfg);
  digitalWrite(CS_PIN, LOW);
  delayMicroseconds(CS_GAP_US);                  // <-- polarity-lock gap
  SPIRTD.transfer(addr & 0x7F);
  uint8_t v = SPIRTD.transfer(0xFF);
  digitalWrite(CS_PIN, HIGH);
  SPIRTD.endTransaction();
  return v;
}
uint16_t rd16(uint8_t addr){
  SPIRTD.beginTransaction(cfg);
  digitalWrite(CS_PIN, LOW);
  delayMicroseconds(CS_GAP_US);
  SPIRTD.transfer(addr & 0x7F);
  uint8_t hi = SPIRTD.transfer(0xFF);
  uint8_t lo = SPIRTD.transfer(0xFF);
  digitalWrite(CS_PIN, HIGH);
  SPIRTD.endTransaction();
  return ((uint16_t)hi << 8) | lo;
}
void wr8(uint8_t addr, uint8_t val){
  SPIRTD.beginTransaction(cfg);
  digitalWrite(CS_PIN, LOW);
  delayMicroseconds(CS_GAP_US);
  SPIRTD.transfer(addr | 0x80);
  SPIRTD.transfer(val);
  digitalWrite(CS_PIN, HIGH);
  SPIRTD.endTransaction();
}

void setup(){
  Serial.begin(115200);
  delay(1200);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  SPIRTD.begin(SCK_PIN, MISO_PIN, MOSI_PIN, -1);   // -1 = NO hardware CS (manual)
  Serial.println();
  Serial.println("=== MAX31865 MANUAL-CS TEST  [RTD-MANCS-v1] ===");

  // DEFINITIVE comms proof: write the config register, read it back.
  wr8(0x00, 0x10);                 // set 3-wire bit only
  uint8_t rb1 = rd8(0x00);
  wr8(0x00, 0x00);                 // clear it
  uint8_t rb2 = rd8(0x00);
  Serial.printf("comms test: wrote 0x10 -> read 0x%02X ; wrote 0x00 -> read 0x%02X  => %s\n",
                rb1, rb2, (rb1==0x10 && rb2==0x00) ? "SPI OK (chip responds!)" : "SPI broken");
  Serial.println();
}

void loop(){
  uint8_t base = 0x80 | (WIRE3 ? 0x10 : 0x00);     // VBIAS on + wire mode
  wr8(0x00, base);
  delay(10);                                       // bias settle
  wr8(0x00, base | 0x20);                          // 1-shot convert
  delay(65);                                       // conversion time
  uint16_t rtd = rd16(0x01);
  uint8_t  flt = rd8(0x07);
  bool faultbit = rtd & 1;
  rtd >>= 1;
  float ohm = (RREF * rtd) / 32768.0;
  float Z = ohm / RNOMINAL;
  float temp = -245.19 + Z*(2.5293 + Z*(-0.066046 + Z*(4.0704e-3 + Z*(-2.0000e-5))));
  Serial.printf("raw=%5u  R=%6.2f ohm  T=%6.2f C / %6.1f F  fault=0x%02X%s%s\n",
                rtd, ohm, temp, temp*9.0/5.0+32.0, flt,
                faultbit ? " [faultbit]" : "",
                (ohm>95.0 && ohm<160.0) ? "   <== CORRECT!" : "");
  if(flt) wr8(0x00, base | 0x02);                  // clear fault
  delay(1000);
}
