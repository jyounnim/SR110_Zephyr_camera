# Lab 03. Camera → TFT Live Preview — Synaptics Astra SR110 (sr100_rdk/sr100/m55)

## Goal of this lab

The camera labs so far (Lab 01/02) only let you check captured images via GDB dumps or statistics logs. This lab **displays the captured camera frame directly on a physical display (ST7789V3 TFT) as a grayscale image**, so you can see "what the camera is looking at right now" on the board alone, with no PC-side tooling. It's a slow live preview refreshing every 1 second, and that interval can be changed instantly via a console shell command, with no rebuild/reflash needed.

> **Hardware verification: done.** Verified on real SR110 RDK hardware: the camera preview fills the entire TFT panel (240x280) with no blank margins, and the refresh interval can be changed live with the `preview interval set <ms>` shell command. Two issues found and fixed during development are documented in [`TROUBLESHOOTING.md`](./TROUBLESHOOTING.md).

## Prerequisites

- As with Lab 01/02, no AI-related west module is needed — this uses pure Zephyr `video_api` + `spi`/`gpio` functionality only.
- Assumes the camera module is already connected to the SR110 RDK board (J23 connector, no extra wiring needed).
- **Requires an ST7789V3 TFT module + level shifter** — see "TFT wiring" below.
- Having done Lab 01/02 will make the camera capture code structure familiar, but this lab is self-contained.

## What this lab investigates

- Whether raw RAW8 Bayer data can be processed directly in application code into a "good enough" image on an embedded display, with no separate image viewer or PC tooling.
- How to fill the entire screen without distorting the image when connecting two devices with different aspect ratios — the camera (wide 480x270) and the TFT panel (tall 240x280).
- Whether two different peripherals (the MIPI camera's I2C1 control lines, the TFT's SPI0) can operate simultaneously within one application with no conflict.

## What you should learn

- How to handle **rotate → cover-crop → scale** in one pass when the camera (wide, 16:9-ish) and the TFT panel (tall, 240x280) have different aspect ratios: a 90-degree rotation aligns the "long side" of both devices, then only the aspect-ratio difference is cropped from the edges (the same concept as CSS `background-size: cover` or a photo app's "fill" crop), filling the whole screen with no blank margin and no distortion.
- How to walk the inverse coordinate transform in order: output pixel coordinates → (rotated canvas coordinates) → (original Bayer coordinates), and the constraint that the final coordinates must land on even (row, col) to align with a 2x2 Bayer block.
- That averaging a 2x2 Bayer block's 4 bytes (R, G, G, B) gives "the approximate brightness of that block" — handling de-Bayering and brightness approximation in a single computation.
- How to convert a single grayscale value into RGB565 (2 bytes/pixel) by setting R=G=B, and filling the 5/6/5-bit channels with bit shifts like `(gray>>3)<<11 | (gray>>2)<<5 | (gray>>3)`.
- The pattern of chunked, line-by-line transfer of large amounts of pixel data (tens to hundreds of KB) under SPI0's 8-byte FIFO constraint.
- Letting the camera stream keep running continuously in the background while the main loop only "samples" some of those frames onto the screen — a practical way to bridge the speed gap between a 30fps camera and a display refreshing at 1Hz or slower.
- Managing the refresh interval with a single `atomic_t` global variable instead of a Kconfig value, and changing it live via a Zephyr shell command (`preview interval set <ms>`) — the practical benefit of experimenting with a parameter instantly with no rebuild/reflash, and why `atomic_t` is needed for two different threads (main loop vs. shell) to safely exchange the same variable.

## Good to know

- The transform done here is not true demosaicing. Real de-Bayering interpolates neighboring pixels to reconstruct a full R/G/B triple per pixel, but this lab simply averages a whole 2x2 block, discarding color information entirely and keeping only brightness (grayscale). The code is much simpler, at the cost of showing no color at all — the same kind of deliberate simplification noted in Lab 02 ("averaging raw RAW8 bytes is not true luma").
- The 90-degree rotation direction (clockwise) is decided solely inside `rotate_to_bayer_coords()` in `tft_preview.c`. If the displayed image looks rotated the wrong way relative to the camera, or mirrored, relative to what you expect, just change this function's coordinate transform (reverse the rotation direction or flip an axis) — the rest of the crop/scale logic is reused as-is.
- SPI0 runs at 4MHz and must send 240x280x2 bytes = 134,400 bytes per frame, so the theoretical minimum transfer time alone is about 270ms (excluding overhead). Setting the refresh interval too short (e.g. under 300ms) can cause the screen transfer itself to fall behind, stretching out the actual refresh period — the 1000ms default accounts for this margin, and you can use `preview interval set <ms>` to find exactly where things start to lag.
- The camera (I2C1) and TFT (SPI0) are on physically separate pins with no conflict between the two peripherals. However, turning on the TFT does cost you the M55's default console (UART1, shared with GPIO23/24) — see "TFT wiring" below.

## What you need

- Synaptics Astra SR110 RDK board + OV02C10 camera module (already connected via J23, no extra wiring needed)
- **ST7789V3 TFT module + level shifter (e.g. TXS0108E)** — SR110 signals are 1.8V while this TFT module is 3.3V, so a level shifter is mandatory. See "TFT wiring" below for details.
- An external USB-TTL adapter — turning on the TFT (SPI0) moves the console to the alternate UART0 pins (GPIO44/45, J24 pins 13/14), so you need this to see logs. (The lab still works without it if you only want to look at the screen.)
- USB cable, PC (WSL2 + west CLI dev environment) or PowerShell (native Windows) build/flash environment

### TFT wiring (SPI0, via level shifter)

SR110-side signals are 1.8V while the ST7789V3 TFT module is 3.3V, so a level shifter (e.g. TXS0108E) is mandatory.

| Signal | Role | SR110 side (1.8V) | Level shifter | Display side (3.3V) |
|---|---|---|---|---|
| VCC | Power | - | - | 3.3V |
| GND | Ground | GND | GND (common) | GND |
| SCL/SCLK | SPI clock | SPI0 CLK (SoC GPIO22, J25 pin 11) | CH_A <-> CH_B | SCL |
| SDA/MOSI | SPI data | SPI0 MOSI (SoC GPIO23, J25 pin 14) | CH_A <-> CH_B | SDA |
| CS | Chip select | SPI0 CS, native hardware CS (SoC GPIO21, J25 pin 12) | CH_A <-> CH_B | CS |
| RES/RST | Reset | SoC GPIO17, J24 pin 3 | CH_A <-> CH_B | RES |
| DC | Data/Command select | SoC GPIO18, J24 pin 4 | CH_A <-> CH_B | DC |
| BLK | Backlight | - | - | 3.3V (direct, bypasses the level shifter) |

Connect the level shifter's low-voltage side (VCCA) to 1.8V and the high-voltage side (VCCB) to 3.3V. If using a TXS0108E, connect **OE to 1.8V (VCCA)**. MISO is not wired — this panel is write-only.

## Concept — capture -> rotate+cover-crop -> grayscale -> SPI transfer

```
video_dequeue() delivers a WQVGA (480x270) RAW8 Bayer frame
   |
   [coordinate math] for each output pixel (240x280) on the panel:
      1) transform to coordinates on a 90-degree-rotated canvas (270x480)
      2) compute a cropped+scaled position, centered, by the aspect-ratio difference between canvas and panel
      3) inverse-transform that position back to an even (row, col) on the original Bayer frame
   |
   [transform] average that position's 2x2 Bayer block (4 bytes) -> a single grayscale value
   |
   [transform] convert the grayscale value into RGB565 (2 bytes) with R=G=B
   |
   [transfer] send line by line (240 pixels = 480 bytes) in 8-byte chunks over SPI0 via st7789_write_data() -- fills the whole panel (240x280)
   |
   video_enqueue() reuses the buffer, k_sleep(current interval), then repeat for the next frame
```

The camera itself keeps capturing in the background at roughly 30fps once `video_stream_start()` runs — the main loop simply pulls out the single most recent frame each time via `video_dequeue()`, applies it to the screen, and sleeps for the currently configured interval (1000ms by default) before pulling the next one. In other words: "the camera keeps shooting, but the screen only shows a sampled subset of it."

### Changing the refresh interval live (shell)

Instead of hardcoding the refresh interval, this lab manages it with an `atomic_t` global variable (`g_preview_interval_ms`) that can be changed instantly via a Zephyr shell command. On the console (external USB-TTL adapter, alternate UART0 pins):

```
uart:~$ preview interval get
preview interval: 1000 ms

uart:~$ preview interval set 2000
preview interval set to 2000 ms (takes effect on the next frame)

uart:~$ preview interval set 300
preview interval set to 300 ms (takes effect on the next frame)
```

Since this takes effect immediately with no rebuild/reflash, you can experiment directly to find the point where the screen looks smooth versus where it's too fast (or the serial link gets too busy) and starts to lag. The valid range is 100-10000ms; values outside that range or non-numeric input just print an error and leave the existing value unchanged.

## Step 1. Build & flash

`lab/` is ready to go. Builds with no extra options.

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <workspace>/SR110_Zephyr_camera/03_tft_preview/lab -d build_camera_tft_preview
```

Flash the combined image with `openocd_flash.py` as usual.

## Step 2. Run & verify

The scene the camera sees is shown in grayscale across the **entire** TFT panel (240x280), refreshing every 1 second by default. The camera image is displayed rotated 90 degrees, and part of the original field of view is cropped off at the edges due to the aspect-ratio difference.

The console (external USB-TTL adapter, alternate UART0 pins) only prints short logs like this:

```
*** Booting Zephyr OS build v4.4.1 ***
[00:00:00.xxx,000] <inf> camera_tft_preview: === SR110 Camera TFT Preview (interval=1000 ms, 'preview interval set <ms>' to change) ===
[00:00:01.xxx,000] <inf> camera_tft_preview: Frame 1: capture -> display
[00:00:02.xxx,000] <inf> camera_tft_preview: Frame 2: capture -> display
[00:00:03.xxx,000] <inf> camera_tft_preview: Frame 3: capture -> display
...
```

Things worth checking:

- Whether the grayscale image fills the **entire** TFT panel (all the way to the top/bottom/left/right edges).
- Whether covering the camera lens by hand darkens the screen.
- Whether the displayed orientation is rotated 90 degrees from the actual camera orientation (this is expected) — if it looks flipped or mirrored in an unexpected way, only `rotate_to_bayer_coords()` needs to change, as noted in "Good to know."
- Whether moving something in front of the camera shows up on screen with roughly a 1-2 second delay (a good way to feel that this is a slow preview, not real-time streaming).
- Whether `Frame N: capture -> display` keeps incrementing steadily with no timeout errors (unlike Lab 02, this lab runs forever with no fixed frame count — it loops until reset).
- Whether changing the interval with `preview interval set <ms>` immediately changes both the screen refresh rate and the log output rate accordingly.

## Verification checklist

- [x] `west build -b sr100_rdk/sr100/m55` builds successfully
- [x] The grayscale preview fills the entire TFT panel (240x280) with no blank margin
- [x] The screen actually reflects the scene (e.g. darkens when the camera is covered)
- [x] The screen refreshes at the 1-second default interval
- [x] Changing the interval with `preview interval set <ms>` actually changes screen/log speed
- [x] (with the external USB-TTL adapter connected) `Frame N: capture -> display` keeps incrementing with no timeout/error

## Next

Continues into Lab 04 (JPEG hardware encoding) and Lab 05 (USB CDC real-time streaming to PC).

See [`TROUBLESHOOTING.md`](./TROUBLESHOOTING.md) for the issues found during development (the half-screen display bug, and the overly chatty logging/refresh problem) and how they were fixed.
