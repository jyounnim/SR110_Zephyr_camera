# Lab 04. Camera → Hardware JPEG Encoding (+ TFT Still Preview) — Synaptics Astra SR110 (sr100_rdk/sr100/m55)

## Goal of this lab

The camera labs so far (Lab 01-03) had application code directly handle RAW8 Bayer raw data (computing statistics, converting to grayscale). This lab uses SR110's built-in **JPEG hardware encoder (`syna,enc-video`)** to confirm that dedicated hardware — not the CPU — compresses camera frames into JPEG. It uses the SDK's `drivers/video/enc` sample almost as-is — an already-verified official sample that works with no modification.

On top of that, it **decodes the JPEG the encoder produced and shows it as a still image on Lab 03's ST7789V3 TFT**. Lab 03 only showed RAW8 Bayer as grayscale, but this time JPEG decoding fully restores color information, so a **real color image** appears on screen.

> **Hardware verification: done.** Verified on real SR110 RDK hardware: both the JPEG hardware encoding and decoding it on-board to show a color still image on the TFT.

## Prerequisites

- Uses pure Zephyr `video_api` only — no AI-related west module is needed.
- Assumes the camera module is already connected to the SR110 RDK board (J23 connector, no extra wiring needed).
- **Requires an ST7789V3 TFT module + level shifter** — the same wiring as Lab 03. If you already did Lab 03, you can reuse the same wiring. (If you only want to check the JPEG encoding itself, you can go as far as the Step 1-2 GDB dump check without a TFT — see "Good to know.")
- An external USB-TTL adapter — turning on the TFT (SPI0) moves the console to the alternate UART0 pins (GPIO44/45, J24 pins 13/14), so you need this to see logs (same constraint as Lab 03).
- Having done Lab 01/02 will make the `video_api` flow (`video_set_format`/`video_enqueue`/`video_stream_start`/`video_dequeue`) familiar; having done Lab 03 will make the TFT wiring and rotate+cover-crop concept familiar.

## What this lab investigates

- Whether SR110's JPEG hardware encoder can produce compressed JPEG directly from camera frames without the application processing RAW8 Bayer itself, unlike Lab 01-03.
- That this encoder actually bypasses `video_syna0` (the general-purpose capture driver used by Lab 01-03) entirely, and instead **wires the sensor's (OV02C10) CSI output directly to the encoder node at the devicetree level** — i.e. the hardware pipeline shortens from the 3-stage "camera → capture driver → encoding" to a 2-stage "camera → encoder."
- That the same encoder node can select between two completely different input paths (live sensor / a static pattern in reserved memory) via a single devicetree `mode` property.
- **Whether a compressed format like JPEG can also be decompressed and shown on screen with a small software decoder on an embedded MCU** — whether the board can, on its own with no GDB dump, do the round trip of "decompress what was just compressed and show it."

## What you should learn

- That the `syna,enc-video` node is an independent `video_api` device separate from `video_syna0` (the MIPI capture driver) — it uses the exact same `video_set_format()`/`video_get_caps()`/`video_import_buffer()`/`video_enqueue()`/`video_stream_start()`/`video_dequeue()` API as Lab 01-03, but the difference is that it configures the **output** format (`VIDEO_PIX_FMT_JPEG`), not an input format.
- The pattern of connecting two devices (the sensor's `ov02c10_out` and the encoder's `lp_jpeg0_in`) directly via devicetree `port`/`endpoint` with `remote-endpoint-label`.
- Why the JPEG encoder needs dedicated reserved memory (`reserved-memory`, `zephyr,memory-attr = <DT_MEM_ARM_MPU_RAM_NOCACHE>`) — the encoder hardware needs a separate non-cacheable memory region it can DMA into directly, and within that region an offset for "storing the raw frame" (`frame-raw-offset`) is split from a size cap for "the max JPEG output" (`max-jpeg-size`).
- The difference between `mode = <0>` (live sensor → encoder → memory) and `mode = <1>` (a synthetic color-bar pattern in reserved memory → encoder → memory) — the latter is a hardware-independent test path that can verify the encoder itself with no camera attached.
- That since compressed output (JPEG) varies in size frame to frame, unlike fixed-size raw data, you always have to check the actual number of encoded bytes via `video_buffer.bytesused` rather than assuming a fixed size.
- **How to handle JPEG decoding with a tiny embedded-focused library (TJpgDec)** — a general-purpose library like libjpeg doesn't fit a small MCU's flash (ITCM) budget, but TJpgDec is only a few KB of code and hands data back one MCU (minimum coded unit) block at a time via callback, which suits streaming processing.
- **A technique for handling downscaling at decode time in one pass** — instead of fully decoding the original 480x270 and then downscaling afterward, TJpgDec's `scale` parameter decodes directly to 1/2 size (240x135) during the IDCT (inverse discrete cosine transform) stage, cutting both decompression compute and framebuffer memory at once.
- That the "rotate → cover-crop" coordinate transform built in Lab 03 is a general-purpose geometric transform reusable regardless of whether the input is RAW8 Bayer or decoded RGB565 — this time the 2x2 Bayer block averaging (de-Bayering) step is simply dropped, while the coordinate transform logic itself stays identical.
- That while Lab 03 was a "live preview" continuously refreshing the screen every frame (once per second), this lab is a **"still preview"** that captures one image once and shows it, then stops — so it doesn't need a shell command to adjust a refresh interval like Lab 03.

## Good to know

- This sample sets `CONFIG_VIDEO_SYNA_MIPI=n` — it doesn't enable the general-purpose MIPI capture driver (`video_syna0`) that Lab 01-03 used at all. That's because the overlay rewires the camera sensor's (`ov02c10`) CSI output endpoint in devicetree to point at the encoder node (`lp_jpeg0`) instead of `video_syna0`. In other words, Lab 01-03 and Lab 04 use the same camera hardware, but **only one pipeline can be active at a time** (they're separate build targets, so they coexist without conflict).
- Building with `mode = <1>` (reserved-memory input) lets you verify the encoder itself with no camera attached at all — it feeds the encoder a synthetic Bayer frame made of vertical color bars (black/blue/cyan/green/magenta/red/yellow/white) plus a checkerboard pattern in the bottom 25%. Since this mode's source is 960x540, its resolution doesn't match this lab's TFT display code (which decodes assuming 480x270 → 240x135), so it isn't shown on the TFT (it's skipped with only a "size mismatch" warning log) — it's only used to verify the JPEG encoder itself.
- JPEG's compression ratio varies by scene every frame, so file size isn't fixed — the SDK's example log shows a 960x540 (mode=1) color-bar pattern compressing to about 31KB, while this lab's measurements (480x270, mode=0) came out smaller than that (see the run log for the exact figure).
- The reserved-memory address (`0xB4900000`) and size (`0x9D800`) are reused as-is from the SDK sample — made up of `0x4000` (a reserved area for the programmer) + `0x7E900` (max size of a 960x540 RAW8 frame, for mode=1) + `0x1AF00` (max JPEG size), so mode=0 (480x270) has extra headroom.
- **TJpgDec (Tiny JPEG Decompressor)** is an open-source library by ChaN (also the author of FatFs), dropped in as-is at `src/tjpgd.c`/`tjpgd.h` (license: no usage restrictions, copyright notice must be kept). Only the config file `src/tjpgdcnf.h` was adjusted for this lab — the output format was changed from the default (RGB888) to **RGB565** (`JD_FORMAT=1`), which the ST7789V3 can consume directly.
- If the decode workspace (`JD_WORK_SIZE`, currently 4096 bytes) is too small, `jd_prepare()`/`jd_decomp()` return a `JDR_MEM1` error — if that happens, increase `JD_WORK_SIZE` in `tft_still.c`. ChaN's documentation says around 3.1KB is usually enough, but the actual requirement can vary with image structure (e.g. presence of restart markers), so some margin was left.
- The screen display reuses Lab 03's "rotate 90 degrees + cover-crop" as-is, but since the input is already-decoded RGB565 pixels rather than RAW8 Bayer, the 2x2 block averaging (de-Bayering) step is skipped — as a result, unlike Lab 03 (grayscale), this lab shows an **actual color image** on screen.
- As with Lab 03, the camera (I2C1) and TFT (SPI0) are on physically separate pins with no conflict. Turning on the TFT still costs the M55's default console (UART1, shared with GPIO23/24), the same constraint as Lab 03.
- Even though this lab newly pulls in TJpgDec, it was confirmed to build/run fine on real hardware with no flash (ITCM) overflow — unlike the flash overflow Lab 03 hit when adding its shell feature (see [`03_tft_preview/doc/TROUBLESHOOTING.md`](../../03_tft_preview/doc/TROUBLESHOOTING.md)), this lab is a one-shot still display with no shell, so its code size appears to have stayed smaller. That's why this lab has no separate troubleshooting document.

## What you need

- Synaptics Astra SR110 RDK board + OV02C10 camera module (already connected via J23, no extra wiring needed)
- **ST7789V3 TFT module + level shifter (e.g. TXS0108E)** — same wiring as Lab 03 (see the table below).
- An external USB-TTL adapter — turning on the TFT (SPI0) moves the console to the alternate UART0 pins (GPIO44/45, J24 pins 13/14), so you need this to see logs.
- USB cable, PC (WSL2 + west CLI dev environment) or PowerShell (native Windows) build/flash environment
- (Optional, to check JPEG encoding without a TFT) OpenOCD + `arm-none-eabi-gdb`, a regular image viewer

### TFT wiring (SPI0, via level shifter — same as Lab 03)

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

Connect the level shifter's low-voltage side (VCCA) to 1.8V and the high-voltage side (VCCB) to 3.3V. If using a TXS0108E, connect **OE to 1.8V (VCCA)**. MISO is not wired.

## Concept — camera → JPEG encoder → (newly added) decode → TFT

```
[mode=0, live sensor path -- this lab's default]

OV02C10 sensor CSI output (ov02c10_out)
   |  wired directly via a devicetree endpoint (remote-endpoint-label)
JPEG encoder (lp_jpeg0, syna,enc-video) input
   |  video_set_format(VIDEO_PIX_FMT_JPEG, 480x270) configures the output format
   |  video_enqueue() registers 2 output buffers (to hold the JPEG)
   |  video_stream_start() starts the encoding pipeline
   |  hardware receives sensor frames, compresses to JPEG, writes to reserved memory
video_dequeue() reclaims the finished JPEG buffer (bytesused = actual compressed size)
   |
   |- (existing) dump the (buffer address, buffer address+bytesused) range with GDB -> save as .jpg -> view on PC
   `- (new) tft_still_show_jpeg(): decode that same JPEG buffer on-board
        |  decode at 1/2 scale with TJpgDec (480x270 -> 240x135 RGB565)
        |  the same rotate (90 degrees) + cover-crop coordinate transform as Lab 03 (de-Bayering step is skipped)
        |  send line by line (240 pixels = 480 bytes) in 8-byte chunks over SPI0 -- fills the whole panel (240x280)
        an actual color still image appears on the TFT
```

The biggest difference is that while Lab 01-03's `video_dequeue()` returned the raw RAW8 Bayer original, this lab's `video_dequeue()` returns an **already-compressed JPEG byte stream**. On top of dumping that JPEG byte stream to a PC as before, this lab also confirms, in one pass, that the board itself can decompress it again and show it on screen (encode → decode → display).

## Step 1. Build & flash

`lab/` is ready to go. The default is live sensor mode (`mode=0`) + TFT still preview.

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <workspace>/SR110_Zephyr_camera/04_jpeg_encode/lab -d build_camera_jpeg_encode
```

(Optional) If you want to check the encoder alone with no camera, you can also build with the reserved-memory color-bar pattern mode (`mode=1`) (this mode is not shown on the TFT — see "Good to know" above):

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <workspace>/SR110_Zephyr_camera/04_jpeg_encode/lab -d build_camera_jpeg_encode_mem -- -DDTS_EXTRA_CPPFLAGS="-DENC_MEMORY_INPUT"
```

Flash the combined image with `openocd_flash.py` as usual.

## Step 2. Run & verify

The console (external USB-TTL adapter, alternate UART0 pins) prints a log similar to this (the actual `bytesused` value varies by scene):

```
*** Booting Zephyr OS build v4.4.1 ***
[00:00:00.xxx,000] <inf> enc_video_sample: JPEG encoder validator start (live-to-memory, mode=0)
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_set_format ret=0 capacity=131072 pitch=0 (480x270)
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_get_caps ret=0 min_vbuf=2 align=64
[00:00:00.xxx,000] <inf> enc_video_sample: enc app_buf[0] addr=0x... size=131072 align=64
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_enqueue[0] ret=0
[00:00:00.xxx,000] <inf> enc_video_sample: enc app_buf[1] addr=0x... size=131072 align=64
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_enqueue[1] ret=0
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_stream_start ret=0
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_dequeue ret=0
[00:00:00.xxx,000] <inf> enc_video_sample: Frame captured at addr=0x... bytesused=NNNNN
[00:00:00.xxx,000] <inf> enc_video_sample: Dump JPEG from 0x... (0x... + NNNNN)
tft_still: JPEG decoded (480x270) and shown on panel
[00:00:0X.xxx,000] <inf> enc_video_sample: JPEG encoder validator done ret=0
```

The `Frame captured at addr=... bytesused=...` value logged here can be used for a GDB dump exactly as before, and once `tft_still: JPEG decoded ... and shown on panel` appears, the on-screen display has also succeeded.

### (Optional) Dumping the JPEG (GDB)

If you also want to check the same image on a PC with no TFT, you can dump it the same way as Lab 01.

**Terminal 1 (OpenOCD)**
```bash
openocd -f boards/syna/astra_sr/sr100/support/openocd.cfg
```

**Terminal 2 (GDB)**
```
arm-none-eabi-gdb
target extended-remote :3333
dump binary memory out.jpg <buffer_address> (<buffer_address> + <bytesused>)
```

For example, if the log shows `Frame captured at addr=0x33ea64c0 bytesused=28450`:
```
dump binary memory out.jpg 0x33ea64c0 (0x33ea64c0 + 28450)
```

Things worth checking:

- Whether `video_set_format`/`video_get_caps`/`video_enqueue`/`video_stream_start`/`video_dequeue` all succeed with `ret=0` and no error logs after building/flashing.
- Whether `bytesused` is non-zero and a reasonable size (a few KB to tens of KB).
- Whether `tft_still: JPEG decoded (480x270) and shown on panel` appears, and whether the scene the camera sees is actually shown on the TFT **in color**.
- Whether the displayed image is rotated 90 degrees and fills the entire panel (240x280) with no blank margin, as in Lab 03.
- (Optional) Open the GDB-dumped `out.jpg` in a PC image viewer and compare it against what's shown on the TFT.
- (Optional, if built with `mode=1`) Whether the vertical color bars + bottom checkerboard pattern encode correctly to JPEG — it's expected to not appear on the TFT, leaving only a "size mismatch" warning log.

## Verification checklist

- [x] `west build -b sr100_rdk/sr100/m55` builds successfully (mode=0, live sensor)
- [x] `video_set_format`/`video_get_caps`/`video_enqueue`/`video_stream_start`/`video_dequeue` all succeed with no errors in the console log
- [x] `bytesused` is a non-zero, reasonable size
- [x] `tft_still: JPEG decoded ... and shown on panel` appears, and the TFT shows a real color still image of the camera's scene
- [x] The displayed image is rotated 90 degrees and fills the whole panel (same geometric transform as Lab 03)
- [ ] (Optional) Also build with `mode=1` (reserved-memory color-bar pattern) to confirm encoder operation with no camera

## Next

Continues into Lab 05 (USB CDC real-time streaming to PC).
