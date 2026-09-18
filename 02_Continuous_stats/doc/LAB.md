# Lab 02. Continuous Capture + Brightness Statistics — Synaptics Astra SR110 (sr100_rdk/sr100/m55)

## Goal of this lab

Extends Lab 01's one-shot 10-frame capture into a much longer continuous capture (100 frames by default), and **computes brightness (mean/max/min) for every frame and logs it**. This is the first step beyond simply "receiving an image from the camera" toward "processing the received data in real time."

> **Hardware verification: done.** Verified on real SR110 RDK hardware: 100 consecutive frames captured with no timeout or error, and the `mean` value was confirmed to change in real time while covering and uncovering the camera by hand.

## Prerequisites

- As with Lab 01, no AI-related west module is needed — this uses pure Zephyr `video_api` functionality only.
- Assumes the camera module is already connected to the SR110 RDK board (J23 connector).
- Having done Lab 01 will make the code structure feel familiar, but this lab is self-contained.

## What this lab investigates

- Whether the camera can reliably stream dozens to hundreds of frames in a short time (a few seconds) with no dropped frames.
- Whether immediately processing (computing statistics on) the raw data every frame causes the capture loop to fall behind or time out — i.e. whether "capture then process" is viable in a real-time pipeline.
- How the brightness of a scene changes frame to frame (by covering the camera or changing lighting), verified numerically.

## What you should learn

- The pattern of the application code directly iterating over the raw buffer received from `video_dequeue()` inside the capture loop to compute statistics (including the constraint that processing must finish before the buffer is re-enqueued).
- A technique for representing an average using only integers, with no float, in embedded code (keeping `mean * 100` as an integer to show two decimal places) — a practical way to print "average"-like values without `CONFIG_REQUIRES_FLOAT_PRINTF`.
- Maintaining per-frame statistics and running statistics over the whole execution simultaneously — accumulating `sum` every frame to produce an exact overall average at the end.
- The limits of treating raw RAW8 Bayer bytes as a "brightness approximation," and why that approximation is good enough for this lab.

## Good to know

- The "brightness" computed here is not true luma (Y) — it's simply **the average of the raw RAW8 Bayer byte values**. In a Bayer pattern each pixel holds only one of R, G, or B, so getting true luma requires de-Bayering (Bayer→RGB) followed by a formula like `Y = 0.299R + 0.587G + 0.114B`. This lab skips that conversion and averages the raw bytes directly, so it's only valid for seeing a trend ("is the scene generally bright or dark, and how does it change over time") — not suitable for precise exposure/metering logic.
- Unlike Lab 01, this lab's Kconfig drops the WQVGA/FHD `choice` entirely and is fixed to WQVGA. Since resolution selection was already covered in Lab 01, this is a deliberate simplification so the code here can focus purely on the capture loop + statistics.
- Techniques like `mean_x100` — multiplying an integer by 100 to represent a decimal fraction — are called "fixed-point" arithmetic. This is common in embedded work when floating-point operations are slow (cores with no FPU) or when code size matters (`CONFIG_REQUIRES_FLOAT_PRINTF` bloats the printf binary).
- Right now, statistics are computed and discarded immediately every frame (the original frame data is not retained) — if you want to view a particular frame that turned out unusually dark or bright as an actual image, you can apply Lab 01's GDB dump method by finding that frame's buffer address in the log and dumping it.

## What you need

Same as Lab 01.

- Synaptics Astra SR110 RDK board + OV02C10 camera module (already connected via J23)
- USB cable, PC (WSL2 + west CLI dev environment) or PowerShell (native Windows) build/flash environment
- Serial terminal (PuTTY / TeraTerm / screen / minicom)

## Concept — computing statistics directly inside the capture loop

Lab 01 logged (`bytesused`, `timestamp`) and immediately reused each frame it received. This lab inserts one step in between.

```
video_dequeue() delivers a frame
   |
   [new step] iterate over the raw bytes to compute mean/max/min
   |
   log the result (per-frame + running total)
   |
   video_enqueue() reuses the buffer (ready for the next frame)
```

One WQVGA frame is 480x270 = 129,600 bytes. A Cortex-M55 has plenty of headroom to simply iterate over data this size, so there's little risk of the statistics computation taking longer than the next frame's arrival — but keep in mind that the pattern of "capturing and processing sequentially in the same loop" can lead to capture timeouts or dropped frames as processing gets heavier (e.g. JPEG encoding, NPU inference), which matters later in Lab 04 (JPEG encoding).

## Step 1. Build & flash

`lab/` is ready to go. Builds with no extra options.

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <workspace>/SR110_Zephyr_camera/02_continuous_stats/lab -d build_camera_stats
```

Flash the combined image with `openocd_flash.py` as usual.

## Step 2. Run & verify

After flashing, logs like the following appear on the serial console. Below is an excerpt of an actual measured log (frames 37-56), captured while covering and then uncovering the camera lens by hand mid-capture — you can see `mean` sitting around 27 (covered) and then jumping sharply starting around frame 50.

```
[00:00:01.716,000] <inf> video_stats_app: Frame   37/100: mean=27.88 max= 82 min= 15 (bytesused=129600)
[00:00:01.749,000] <inf> video_stats_app: Frame   38/100: mean=27.88 max= 81 min= 14 (bytesused=129600)
[00:00:01.782,000] <inf> video_stats_app: Frame   39/100: mean=27.92 max= 82 min= 14 (bytesused=129600)
[00:00:01.815,000] <inf> video_stats_app: Frame   40/100: mean=27.90 max= 83 min= 15 (bytesused=129600)
[00:00:01.848,000] <inf> video_stats_app: Frame   41/100: mean=27.88 max= 82 min= 14 (bytesused=129600)
[00:00:01.882,000] <inf> video_stats_app: Frame   42/100: mean=27.88 max= 83 min= 15 (bytesused=129600)
[00:00:01.915,000] <inf> video_stats_app: Frame   43/100: mean=27.86 max= 84 min= 15 (bytesused=129600)
[00:00:01.948,000] <inf> video_stats_app: Frame   44/100: mean=27.84 max= 83 min= 14 (bytesused=129600)
[00:00:01.981,000] <inf> video_stats_app: Frame   45/100: mean=27.85 max= 84 min= 15 (bytesused=129600)
[00:00:02.014,000] <inf> video_stats_app: Frame   46/100: mean=27.83 max= 83 min= 15 (bytesused=129600)
[00:00:02.048,000] <inf> video_stats_app: Frame   47/100: mean=27.88 max= 83 min= 15 (bytesused=129600)
[00:00:02.081,000] <inf> video_stats_app: Frame   48/100: mean=28.23 max= 88 min= 15 (bytesused=129600)
[00:00:02.114,000] <inf> video_stats_app: Frame   49/100: mean=31.01 max=102 min= 15 (bytesused=129600)
[00:00:02.147,000] <inf> video_stats_app: Frame   50/100: mean=41.09 max=255 min= 16 (bytesused=129600)
[00:00:02.180,000] <inf> video_stats_app: Frame   51/100: mean=68.82 max=255 min= 16 (bytesused=129600)
[00:00:02.213,000] <inf> video_stats_app: Frame   52/100: mean=91.67 max=255 min= 17 (bytesused=129600)
[00:00:02.247,000] <inf> video_stats_app: Frame   53/100: mean=107.22 max=255 min= 17 (bytesused=129600)
[00:00:02.280,000] <inf> video_stats_app: Frame   54/100: mean=114.90 max=255 min= 18 (bytesused=129600)
[00:00:02.313,000] <inf> video_stats_app: Frame   55/100: mean=119.88 max=255 min= 18 (bytesused=129600)
[00:00:02.346,000] <inf> video_stats_app: Frame   56/100: mean=124.03 max=255 min= 18 (bytesused=129600)
```

You can also see that the inter-frame interval is about 33ms (`00:00:01.716` -> `00:00:01.749`, etc.), meaning WQVGA capture is arriving steadily at around 30 frames per second.

Things worth checking:

- `Video format configured: 480x270 ...` — configured with the same resolution/format as Lab 01.
- `Frame N/100:` is logged all 100 times, running to completion with no timeout errors.
- Re-running the capture while covering the lens or pointing it at a bright area causes `mean` to move down/up accordingly (the most intuitive way to confirm the brightness stats actually reflect the scene).
- `=== Summary over 100 frames: ... ===` is printed at the end, and it exits cleanly with `Camera stats sample finished`.

## Verification checklist

- [x] `west build -b sr100_rdk/sr100/m55` builds successfully
- [x] `Video format configured: 480x270 ...` appears on console after flashing
- [x] `Frame N/100:` logged all 100 times, no timeout/error
- [x] `mean` actually changes when covering the camera or changing lighting
- [x] Clean exit after `=== Summary over 100 frames ===`

## Next

Lab 03 displays the captured frames as a real-time grayscale preview on the ST7789V3 TFT (SPI0), updating every 1 second. Then Lab 04 (JPEG hardware encoding) and Lab 05 (USB CDC real-time streaming to PC) follow.
