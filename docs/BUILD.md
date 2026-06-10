# Building from source

## Wiring

```
MH-Z19       Flipper Zero
───────      ────────────
5V      ──►  5V  (pin 1)
GND     ──►  GND (pin 8)
PWM     ──►  A6  (pin 3)
```

## Build steps

```bash
# Clone this repo
git clone https://github.com/razerhome/flipper-zero-co2-mh-z19.git

# Clone Flipper Zero firmware
git clone --recursive https://github.com/flipperdevices/flipperzero-firmware.git

# Copy app to firmware
mkdir -p flipperzero-firmware/applications_user/co2_detector_mh_z19
cp flipper-zero-co2-mh-z19/* flipperzero-firmware/applications_user/co2_detector_mh_z19/

# Build
cd flipperzero-firmware
./fbt fap_co2_detector_mh_z19
```

The compiled `.fap` will be in `build/f7-firmware-D/.extapps/co2_detector_mh_z19.fap`.

## Graph compression table

| Interval | Buffer covers |
|----------|--------------|
| 5 sec | ~10 min |
| 10 sec | ~21 min |
| 20 sec | ~42 min |
| 40 sec | ~1.4 hours |
| 80 sec | ~2.8 hours |
| 160 sec | ~5.7 hours |
| 320 sec | ~11.4 hours |
