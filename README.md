# CO2 Detector MH-Z19 for Flipper Zero

Flipper Zero application for measuring CO2 concentration using the MH-Z19 sensor via PWM.

Forked from [meshchaninov/flipper-zero-mh-z19](https://github.com/meshchaninov/flipper-zero-mh-z19) and significantly improved with signal filtering, calibration, a history graph, and multi-screen navigation.

See [SCREENSHOTS.md](SCREENSHOTS.md) for app screenshots and a wiring photo.

## Features

- **Stable readings** — 4-stage filter pipeline: validation, median (8 samples), EMA smoothing, and status hysteresis
- **Fast PWM polling** — 1 ms GPIO sampling for accurate pulse width measurement
- **Calibration offset** — adjustable offset (up to plus/minus 500 ppm, step 5) to match a reference sensor
- **CO2 history graph** — real-time line chart with auto-scaling time axis (10 minutes to over 11 hours)
- **Debug screen** — raw PWM values, timing, and reading count
- **LED and vibro alerts** — green, yellow, and red status based on 800 and 1000 ppm thresholds with hysteresis

## What's changed from the original

- **Readings** — original showed raw PWM jumping by about 50 ppm; this fork uses a 4-stage filter (median and EMA) for stable readings within 1-2 ppm
- **GPIO polling** — original polled about every 100 ms (caused value freezing); this fork polls every 1 ms for accurate PWM capture
- **Screens** — original had one measurement screen; this fork has five: connect, calibrate, measure, debug, and graph
- **Calibration** — added an adjustable offset of up to plus/minus 500 ppm
- **History** — added a 128-point graph with auto-compression (10 minutes to 11 hours)
- **Alerts** — original had a basic LED; this fork adds LED and vibro with hysteresis to prevent flickering

## Screens

- **Connect** — wiring instructions; press OK for next
- **Calibrate** — adjust the ppm offset with left and right; press OK for next
- **Measure** — CO2 value, status icon, and offset; press Up for debug, Right for graph
- **Debug** — raw ppm, Th and Tl timing, readings count; press Down to go back
- **Graph** — CO2 history chart with thresholds; press Left to go back

## Wiring

Connect the MH-Z19 sensor to the Flipper Zero GPIO pins:

- 5V to 5V (pin 1)
- GND to GND (pin 8)
- PWM to A6 (pin 3)

See [SCREENSHOTS.md](SCREENSHOTS.md) for a wiring photo.

## Installation

**Option 1 — Download the prebuilt app (recommended):**

- Download the .fap file from the project Releases page
- Copy it to your Flipper SD card under apps/GPIO
- Open Applications, then GPIO, then CO2 detector MH-Z19

**Option 2 — Build from source:**

Clone this repository and the Flipper Zero firmware, copy the app into the firmware applications_user folder, and build it with fbt. Full step-by-step build commands are in [docs/BUILD.md](docs/BUILD.md).

## How the graph works

The graph stores 128 data points. Recording starts at 5-second intervals. When the buffer is full, it compresses by averaging pairs of points and doubling the interval, so 128 points can cover from about 10 minutes up to over 11 hours of monitoring.

Approximate coverage as the interval grows:

- 5 seconds per point — about 10 minutes
- 10 seconds per point — about 21 minutes
- 20 seconds per point — about 42 minutes
- 40 seconds per point — about 1.4 hours
- 80 seconds per point — about 2.8 hours
- 160 seconds per point — about 5.7 hours
- 320 seconds per point — about 11.4 hours

## License

MIT License. Based on original work by [Nikita Meshchaninov](https://github.com/meshchaninov).
