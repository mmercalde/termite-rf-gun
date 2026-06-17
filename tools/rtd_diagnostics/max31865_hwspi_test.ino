// max31865_hwspi_test  —  HARDWARE SPI  [tag: RTD-HW-v1]
// Same MAX31865 (generic chip), but driven by the ESP32-S3's SPI PERIPHERAL
// instead of bit-banged GPIOs. The peripheral sets the correct SPI mode the
// chip requires; the software bit-bang path may not, which produces exactly
// the rail-value garbage (raw 0/32767, flt 0x00/0xFF) we've been seeing.
//
// Wiring (UNCHANGED):
//   VIN -> 3.3V (clean/bench)   GND -> GND (common)
//   CLK -> GPIO11   SDO -> GPIO13   SDI -> GPIO12   CS -> GPIO1   RDY -> n/c
//
// PT100 board: RREF 430, RNOMINAL 100.
// Note: in the MAX31865, 2-wire and 4-wire are the SAME chip setting; only
// 3-wire differs. So we test just two configs: "2/4-wire" and "3-wire".

#include <SPI.h>
#include <Adafruit_MAX31865.h>

#define CS_PIN   1
#define SCK_PIN  11     // CLK
#define MISO_PIN 13     // SDO
#define MOSI_PIN 12     // SDI

#define RREF      430.0
#define RNOMINAL  100.0

SPIClass SPIRTD(FSPI);                    // dedicated hardware SPI bus
Adafruit_MAX31865 rtd(CS_PIN, &SPIRTD);   // HARDWARE SPI constructor

void readCfg(max31865_numwires_t w, const char* name){
  rtd.begin(w);
  delay(20);
  uint16_t raw = rtd.readRTD();
  float ohm = (RREF * raw) / 32768.0;
  float c   = rtd.temperature(RNOMINAL, RREF);
  uint8_t f = rtd.readFault();
  Serial.printf("  %-9s raw=%5u  R=%6.2f ohm  T=%7.2f C / %6.1f F  flt=0x%02X%s\n",
                name, raw, ohm, c, c*9.0/5.0+32.0, f,
                (ohm>95.0 && ohm<160.0) ? "   <== READS CORRECT" : "");
  if(f) rtd.clearFault();
}

void setup(){
  Serial.begin(115200);
  delay(1200);
  SPIRTD.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);   // map peripheral to our pins
  Serial.println();
  Serial.println("=== MAX31865 HARDWARE-SPI TEST  [RTD-HW-v1] ===");
  Serial.printf("CS=%d CLK=%d SDO=%d SDI=%d  RREF=%.0f (PT100)  bus=hardware\n",
                CS_PIN, SCK_PIN, MISO_PIN, MOSI_PIN, RREF);
  Serial.println("Want R ~109 ohm / ~24 C, STEADY (not flipping).");
  Serial.println();
}

void loop(){
  readCfg(MAX31865_2WIRE, "2/4-wire");
  readCfg(MAX31865_3WIRE, "3-wire");
  Serial.println();
  delay(1000);
}
