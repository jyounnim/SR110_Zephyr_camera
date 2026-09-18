# Lab 01. MIPI Camera Frame Capture Basics — Synaptics Astra SR110 (sr100_rdk/sr100/m55)

## Goal of this lab

Capture frames from the **OV02C10 sensor (MIPI CSI-2, 1-lane)** wired to the SR110's on-board camera connector (J23), using Zephyr's standard `video_api`, and confirm capture success, size, and timestamp over the console log. With no NPU/AI involved, this is the "Hello World" of this camera lab series — just receiving a single image from the camera.

> **Hardware verification: done.** Build, flash, and 10-frame capture have all been confirmed working on real SR110 RDK hardware (see "Run & verify" below).

## Prerequisites

- No AI-related west module (e.g. TFLM) is needed for this lab — camera capture is pure Zephyr `video_api` functionality and needs no extra module.
- Assumes the camera module is already connected to the SR110 RDK board (J23 connector). Connect it first if it isn't.

## What this lab investigates

- Whether Zephyr's standard `video_api` flow (`video_get_caps` → `video_set_format` → enqueue buffers → `video_stream_start` → repeated `video_dequeue` → `video_stream_stop`) actually works against SR110's MIPI CSI-2 camera.
- How many milliseconds the sensor actually takes to deliver each frame, and whether the captured raw data size matches the expected resolution × format.

## What you should learn

- The standard Zephyr `video_api` capture pattern (prepare buffers → enqueue → stream start → dequeue → reuse/release).
- How a single MIPI CSI-2 camera is split across two separate buses on SR110: **control (SCCB, I2C1)** and **video data (CSI-2 lanes)** are physically separate.
- What the RAW8 Bayer format (`VIDEO_PIX_FMT_SRGGB8`) is, and why the camera outputs this instead of RGB directly.
- How the camera driver (`video_syna0`, `syna,mipi-video`) and the sensor (`ov02c10`) are represented in devicetree, and that `status = "okay"` means enabled by default.

## Good to know

- This sample is designed to select resolution via a Kconfig `choice` (WQVGA/FHD). FHD (1920x1080) needs a reserved-memory (SHM) devicetree overlay because a single frame is ~2MB — this lab only covers WQVGA (480x270), which needs no overlay. If you want to try FHD, see `boards/sr100_rdk_m55_fhd.overlay` in the original SDK sample (`zephyr_srsdk/samples/drivers/video/mipi_capture`).
- The captured raw frame is never printed to the console — it only exists in board memory. To actually see it as an image, you need to dump the memory over OpenOCD+GDB to the PC and de-Bayer it with `ffplay` — see "Step 3" below.
- Since `video_dequeue()` hands back the same buffers you enqueued, this sample's buffer-reuse pattern is to retain only the last frame separately (`retain_capture_buffer`) and immediately re-enqueue the rest — a common design when the buffer pool is limited, as with cameras.
- Sensor power/reset is controlled through a GPIO expander (`pca6416@20`) (`powerdown-gpios`, `shutdown-gpios`) — a common root cause when the camera doesn't respond is an initialization issue around this GPIO expander.

## What you need

- Synaptics Astra SR110 RDK board + OV02C10 camera module (already connected via J23, no extra wiring needed)
- USB cable, PC (WSL2 + west CLI dev environment)
- Serial terminal (PuTTY / TeraTerm / screen / minicom)
- (Optional) To view the frame as an actual image: OpenOCD, `arm-none-eabi-gdb`, `ffplay` (part of the ffmpeg package)

## Concept — SR110's camera data path

A single camera actually connects to SR110 over two independent buses.

```
[OV02C10 sensor]
   |- Control (SCCB, 2-wire) --> SR110 I2C1 (ov02c10@36, register config / power control)
   `- Video data (MIPI CSI-2, 1-lane) --> SR110 CSI-2 input --> video_syna0 driver (syna,mipi-video)
```

- The `ov02c10` devicetree node sits on I2C1 and controls the sensor's init register values, resolution settings, power (via the GPIO expander), and MCLK.
- The `video_syna0` devicetree node (`compatible = "syna,mipi-video"`) is the capture engine that receives video data arriving on the actual CSI-2 lanes and DMAs it into a specified memory buffer.
- The application only ever has to deal with `video_syna0` through the standard Zephyr `video_api` — I2C1 control is fully handled inside the driver and never needs to be touched from application code.
- The raw data the sensor emits is not RGB but **RAW8 Bayer** (`VIDEO_PIX_FMT_SRGGB8`) — each pixel only holds one of R/G/B, and turning it into a real color image requires de-Bayering (interpolation) on the PC side. This lab doesn't bother doing that conversion on the board; `ffplay` does it for us when needed.

## Step 1. Build & flash

`lab/` is ready to go. WQVGA is the Kconfig default resolution, so it builds with no extra options.

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <workspace>/SR110_Zephyr_camera/01_basic_capture/lab -d build_camera_capture
```

Flash the combined image with `openocd_flash.py` as usual.

## Step 2. Run & verify (measured on real SR110 RDK)

This is the log actually observed on the serial console after flashing (addresses/timings will vary per run).

```
*** Booting Zephyr OS build v4.4.1 ***
[00:00:00.357,000] <inf> video_sample_app: [TS] MIPI_CAPTURE_MAIN_ENTER uptime_ms=357
[00:00:00.361,000] <inf> video_sample_app: Zephyr RAM window: [0x33eb9460..0x3416f000)
[00:00:00.366,000] <inf> video_sample_app: Sample frame dump buffer: 129600 bytes at 0x33ecb4bc
[00:00:00.371,000] <inf> video_sample_app: Video caps: min_vbuf_count=2 align=64
[00:00:00.453,000] <inf> video_sample_app: Video format configured: 480x270 pitch=480 size=129600
[00:00:00.460,000] <inf> video_syna_sr100_mipi: Using internal SHM pool: addr=0x33eeaf40 size=524288
[00:00:00.465,000] <inf> video_syna_sr100_mipi: Sensor input bpp=10, driver output pixelformat=0x42474752
[00:00:00.470,000] <inf> video_syna_sr100_mipi: Configuring csi=0 shm=0x33eeaf40 size=129600
[00:00:00.476,000] <inf> video_syna_sr100_mipi: Stream started (480x270 size=129600)
[00:00:00.496,000] <inf> video_sample_app: Video stream started in 139 ms
[00:00:00.500,000] <inf> video_sample_app: Waiting for frame 1/10...
[00:00:00.525,000] <inf> video_sample_app: Frame 1 captured: bytesused=129600 timestamp=525 buffer=0x33f6afc0
[00:00:00.530,000] <inf> video_sample_app: Frame 1 captured and available in RAM in 26 ms
[00:00:00.540,000] <inf> video_sample_app: Stored captured frame: 129600 bytes at 0x33ecb4bc
...
[00:00:00.810,000] <inf> video_sample_app: Waiting for frame 10/10...
[00:00:00.823,000] <inf> video_sample_app: Frame 10 captured: bytesused=129600 timestamp=823 buffer=0x33f8aa40
[00:00:00.833,000] <inf> video_sample_app: Stored captured frame in-place: 129600 bytes at 0x33f8aa40
[00:00:00.843,000] <inf> video_syna_sr100_mipi: Stream stopped
[00:00:00.846,000] <inf> video_sample_app: Video sample finished
[00:00:00.849,000] <inf> video_sample_app: Sample completed in 492 ms from start
```

Things worth confirming:

- `Video format configured: 480x270 pitch=480 size=129600` — configured with the requested resolution/format.
- `Frame N captured: bytesused=...` is printed all 10 times (default `VIDEO_SAMPLE_CAPTURE_COUNT=10`), consistently `bytesused=129600` every time — no dropped frames or size mismatches.
- The entire stream, from first to last frame, finishes in about 492ms (stream start alone takes 139ms; the 10-frame capture itself is much faster).
- Clean shutdown with no errors: `Stream stopped` → `Video sample finished`.
- `driver output pixelformat=0x42474752` is ASCII `"BGRG"` (a little-endian FourCC, actually representing the `"GRGB"` Bayer order) — confirms the driver internally keeps the RAW8 Bayer format.
- Note: the `video_ov02c10` init logs (pinctrl/power GPIO/MCLK, etc.) are at `<dbg>` level and don't show up at `CONFIG_LOG_DEFAULT_LEVEL=3` (info) — that's expected; since frames are actually arriving, sensor init itself is fine.

## Step 3. Viewing a frame as an image (optional, hardware-verified)

The last (10th) frame is retained by `store_captured_frame()`, so you can dump it directly using the address printed in `Stored captured frame: 129600 bytes at 0x...`.

**From PowerShell (native Windows)** — this is the method actually verified this time. Use the same `openocd.exe`/`.cfg` you use for flashing with `openocd_flash.py`.

```powershell
# Window 1: run OpenOCD in server mode (not the flashing wrapper — invoke openocd.exe directly with just the cfg)
& "<path to openocd.exe>" -f "<...>\syna_zephyr\srsdk_tools\Input_Config\sr100_m55.cfg"
# Leave this window open once you see "Listening on port 3333 for gdb connections".
```

After installing the Windows Arm GNU toolchain (`arm-none-eabi-gdb.exe`, from the [official Arm download](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)), in a new PowerShell window:

```powershell
$env:Path += ";C:\arm-gnu-toolchain\bin"   # adjust to your install path
arm-none-eabi-gdb
target extended-remote :3333
dump binary memory frame_dump.raw <address from log> (<address from log> + 129600)
```

Preview the RAW8 Bayer dump, de-Bayered, with Windows ffmpeg (ffplay included, from [ffmpeg.org](https://ffmpeg.org/download.html#build-windows)):

```powershell
ffplay -hide_banner -loglevel error -f rawvideo -pixel_format bayer_rggb8 -video_size 480x270 frame_dump.raw
```

**From WSL (Linux)** — for running OpenOCD inside WSL (when the USB probe was passed through with `usbipd`).

```bash
# Terminal 1: run OpenOCD (from the root of the zephyr_srsdk repo)
openocd -f boards/syna/astra_sr/sr100/support/openocd.cfg

# Terminal 2: connect with GDB and dump (gdb-multiarch or arm-none-eabi-gdb)
gdb-multiarch
target extended-remote :3333
dump binary memory frame_dump.raw <address from log> (<address from log> + 129600)
```

```bash
# Preview the RAW8 Bayer dump, de-Bayered
ffplay -hide_banner -loglevel error -f rawvideo -pixel_format bayer_rggb8 -video_size 480x270 frame_dump.raw
```

> **Note**: OpenOCD and GDB must run in the same environment (both PowerShell or both WSL) — `localhost:3333` may not connect across environments depending on your WSL2 network setup.

## Verification checklist

- [x] `west build -b sr100_rdk/sr100/m55` builds successfully
- [x] `Video format configured: 480x270 ...` appears on console after flashing
- [x] `Frame N captured` logged 10 times, `bytesused=129600` consistently
- [x] Clean exit with `Video sample finished`
- [x] GDB dump + `ffplay` confirms an actual captured image (verified from PowerShell)

## Next

Continues into Lab 02 (continuous capture + brightness stats), Lab 03 (camera → TFT live preview), Lab 04 (JPEG hardware encoding), and Lab 05 (USB CDC real-time streaming to PC).
