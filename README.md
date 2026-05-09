# Literary Clock


Display a different literary quote every minute on an e-paper display. Each hour of the day has 60 unique quotes from literature, philosophy, and poetry—a minimalist desk clock that doubles as daily inspiration.

**Now supports the Elecrow CrowPanel 4.2" ESP32-S3 e-paper display (SSD1680, 400×300 px, monochrome, JST-PH 2.0mm LiPo connector).**



## Hardware

- **Elecrow CrowPanel 4.2" ESP32-S3** (recommended)
  - 400×300 px e-paper display (SSD1680 controller, monochrome)
  - JST-PH 2.0mm LiPo battery connector (3.7V, 500–1000mAh recommended)
  - USB-C for power/programming
- **ESP32-S3** microcontroller (other variants may work)
- **E-paper display**: 4.2" (400×300 px, SSD1680) or 2.13" (250×122 px, legacy)

> This project is optimized for the Elecrow CrowPanel 4.2" ESP32-S3. Other SSD1680-based displays may work with minor code changes.

## Quick Start

### 1. Prepare Arduino IDE
- Install ESP32 board support in Arduino IDE
- Copy these files into your sketch folder:
  - `EPD.cpp`, `EPD.h` — e-paper graphics and text rendering
  - `EPD_Init.cpp`, `EPD_Init.h` — display initialization, SSD1680 driver, 400×300 px
  - `spi.cpp`, `spi.h` — bit-banged SPI (renamed to avoid Arduino SPI.h collision)
  - `EPDDisplay.h` — Adafruit GFX wrapper for high-quality proportional fonts

### 2. Upload Quote Data
- Copy all files from the `data/` folder to your ESP32 via Arduino IDE's LittleFS uploader
- Each file (`0.txt`–`23.txt`) contains 60 quotes for that hour of the day
- File format: one quote per line, pipe-delimited: `Quote|Author|Book Title`

### 3. Flash & Configure
1. Upload `literary_clock.ino` to your ESP32
2. On first boot, a WiFiManager captive portal appears—connect to configure WiFi
3. Device auto-detects timezone via IP geolocation; falls back to `EST5EDT` if detection fails
4. Every 60 wakeups (~1 hour), time is re-synced via NTP

### Reset WiFi
Hold the BOOT button for 3 seconds at startup to clear saved WiFi credentials and re-run the captive portal.

## How It Works

**Every Minute:**
1. ESP32 wakes from deep sleep
2. Gets current time (or syncs with NTP if it's been ~1 hour)
3. Looks up the quote for `[hour].txt` at line `[minute]`
4. Parses quote, author, and book title (pipe-delimited)
5. Renders text to display: quote (top), time (top-right), attribution (bottom)
  - Uses Adafruit GFX proportional fonts for crisp, readable text
  - Dynamically scales font size and wraps text to fit 400×300 px
  - Attribution line is rendered smaller for clarity
6. Updates e-paper display (very low power)
7. Sleeps until next minute

**WiFi & Time:**
- Uses **WiFiManager** for zero-config WiFi setup
- **NTP** syncs time with `pool.ntp.org`
- **IP geolocation** auto-detects timezone; supports ~30 common zones with proper DST rules
- Timezone info survives deep sleep; time does not (resyncs on wake)

**Power:**
- E-paper updates consume power, but deep sleep between updates is extremely efficient
- Typical daily usage: <15 mA average
- 3.7V LiPo battery (JST-PH 2.0mm, 500–1000mAh recommended)
- **Check battery polarity before connecting!**

## Data Format

Each hour file (`0.txt`, `1.txt`, … `23.txt`) contains exactly 60 lines (one per minute).

```
Quote text here|Author Name|Book Title or Source
```

Example:
```
It was the best of times, it was the worst of times.|Charles Dickens|A Tale of Two Cities
```

All three fields are optional (e.g., just a quote with no author), but the pipe format is required.

**Data Included:**
- 1,440 unique literary quotes (60 per hour)
- Sourced from classic literature, philosophy, and poetry
- Pre-formatted with HTML line breaks cleaned up

## Housing

Included **3D CAD and STL files** for a simple printed enclosure:
- `SCAD/literary_clock_case_small.scad` — OpenSCAD source (edit size/features here)
- `SCAD/literary_clock_case_small.stl` — Pre-sliced STL ready to print
- Holds ESP32 board and e-paper display, mounts on desk

Print settings:
- Nozzle: 0.4 mm
- Layer height: 0.2 mm
- Infill: 20–30%
- Support: varies by orientation

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Display stays blank | Ensure `data/` files uploaded to LittleFS. Check serial output for errors. |
| "No time sync" error | WiFi/NTP not available. Device will retry on next boot. Ensure SSID saved. |
| WiFi won't connect | Hold BOOT button 3 seconds to reset credentials. Re-enter network info. |
| Wrong timezone | IP geolocation failed. Edit `TZ_STRING` at top of sketch to your POSIX timezone string. |
| Time drifts | NTP re-syncs every ~1 hour automatically. Check `pool.ntp.org` accessibility. |


## Architecture

- **`literary_clock.ino`** — Main logic: wake → sync time → get quote → render → sleep
- **`EPD.*`** — E-paper graphics primitives, text rendering (now uses Adafruit GFX)
- **`EPD_Init.*`** — SSD1680 e-paper driver, hardware init for ESP32-S3 GPIO & SPI
- **`spi.*`** — Bit-banged SPI protocol (avoids Arduino SPI.h collision)
- **`EPDDisplay.h`** — Adafruit GFX wrapper for proportional fonts, word wrap, scaling


## Configuration

Edit the top of `literary_clock.ino`:

```cpp
const char* TZ_STRING           = "EST5EDT,M3.2.0,M11.1.0";  // Fallback timezone
const char* NTP_SERVER          = "pool.ntp.org";
#define NTP_SYNC_INTERVAL_MIN   60  // Re-sync every 60 wakeups
#define RESET_BUTTON_PIN        0   // BOOT button GPIO
```

Supported POSIX timezones include US, Europe, Asia-Pacific regions. Add custom zones in `buildPOSIXtz()`.

## License & Attribution

Quote dataset adapted from [JohannesNE/lit-review](https://github.com/JohannesNE/lit-review).

E-paper driver and hardware initialization adapted for Elecrow CrowPanel 4.2" ESP32-S3 (SSD1680, 400×300 px).

Elecrow EPD driver files included per manufacturer license.
