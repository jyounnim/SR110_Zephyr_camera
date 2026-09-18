# Lab 03 Troubleshooting — Camera → TFT Live Preview

Two issues found and fixed during hardware verification.

## 1. Only half the TFT screen was displayed

**Symptom**: The initial version simply divided the camera frame (480x270) by 2 to compute a 240x135 image and drew it only in the top-left corner of the panel. As a result, the bottom half of the panel (rows 135-280) was always left blank (the init color).

**Cause**: The camera (wide, 480x270) and the TFT panel (tall, 240x280) have different aspect ratios, but only a simple downscale (÷2) was applied with no handling of that aspect-ratio difference. 240x135 happened to match the panel's width (240) but came to less than half of its height (280).

**Fix**: Redesigned around rotating the camera image 90 degrees to align the camera's long side (480) with the panel's long side (280), then applying a "cover crop" (the same concept as CSS `background-size: cover` or a photo app's "fill" crop) centered on the aspect-ratio difference. The coordinate transform works backward from output pixels (240x280), to rotated-canvas coordinates (270x480), to the original Bayer coordinates, snapping the final coordinates to even values to align with a 2x2 Bayer block. This fills the entire panel with no blank margin, at the cost of always cropping off part of the original field of view (by however much the aspect ratios differ after rotation) — a good example of how filling the screen and showing the full field of view can't both be satisfied at once when aspect ratios differ.

## 2. Screen/serial logging was too chatty to read

**Symptom**: The initial design printed several log lines per frame capture (video caps, format, a summary every 10 frames, etc.). At short refresh intervals, the serial output got interleaved with screen updates, making both the console and the screen hard to follow.

**Cause**: This inherited the detailed logging style from Lab 01/02, but this lab is an infinite-loop live preview, so logs kept accumulating without end. The initial refresh interval (0.5s) also left little margin against the SPI0 transfer time (theoretically at least ~270ms for the full panel).

**Fix**: Trimmed logging down to a single `Frame N: capture -> display` line per cycle, aside from the one-time init log at boot. Also increased the default refresh interval from 0.5s to 1s to give the SPI transfer more headroom. Rather than a Kconfig constant, this interval is managed via an `atomic_t` global variable plus a Zephyr shell command (`preview interval get/set`), so it can be tuned live with no rebuild/reflash to find exactly where the screen starts to lag.
