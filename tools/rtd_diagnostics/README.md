# RTD (MAX31865) diagnostic sketches

Standalone test sketches used during the 2026-06-15 MAX31865 bring-up.
See `docs/failure_analysis/2026-06-15_max31865_rtd_bringup_blocked.md` for the
full story. Each is self-contained; wire one MAX31865 board, flash, read serial.

Wiring (all three): VIN→3.3V (the VIN pin), GND→GND, CLK→GPIO11, SDO→GPIO13,
SDI→GPIO12, CS→GPIO1. PT100, RREF 430.

- `max31865_single_test`   [RTD-SOLO-v2] — one board, software SPI, sweeps 2/3/4-wire each second, flags the mode reading ~109 Ω.
- `max31865_hwspi_test`    [RTD-HW-v1]   — same, but driven by the ESP32-S3 hardware SPI peripheral (correct SPI mode).
- `max31865_manualcs_test` [RTD-MANCS-v1]— bare-register, MANUAL CS with an explicit CS-low→first-clock gap (the documented polarity-lock fix), plus a config write/read-back comms proof. **Run this first on a known-good ESP32** — its `comms test:` line settles board-vs-CS-timing.
