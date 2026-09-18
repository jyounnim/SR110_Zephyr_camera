# SR110 Zephyr Camera Labs (SR110_Zephyr_camera)

A 5-part lab series that walks through **how to use a MIPI camera with Zephyr RTOS** on the Synaptics Astra **SR110** RDK board (`sr100_rdk/sr100/m55`, Cortex-M55 + Ethos-U55), from the ground up. NPU/AI inference is out of scope here — the focus is purely on camera fundamentals: receiving, processing, displaying, and transmitting images from the camera. Camera-based AI labs (NPU inference, etc.) continue in a separate curriculum.

## Lab list

| # | Lab | Summary | Hardware verified |
|---|---|---|---|
| [01](./01_basic_capture/) | Camera frame capture basics | Capture 10 frames via Zephyr `video_api`, verify the image via a GDB dump | ✅ Done |
| [02](./02_continuous_stats/) | Continuous capture + brightness statistics | Continuously capture 100 frames, computing brightness (mean/max/min) every frame | ✅ Done |
| [03](./03_tft_preview/) | Camera → TFT live preview | Show a grayscale live preview on an ST7789V3 TFT (1s interval, adjustable via shell) | ✅ Done |
| [04](./04_jpeg_encode/) | JPEG hardware encoding + TFT still preview | Compress with the hardware JPEG encoder, decode on-board, and show a color still image | ✅ Done |
| [05](./05_usb_stream/) | Real-time streaming to a PC over USB CDC | Continuously send JPEG frames to a PC over USB CDC ACM, displayed live by a Python viewer | ✅ Done |

Each lab is documented to be doable on its own, but working through them in order (01 -> 05) builds concepts cumulatively and is easier to follow.

## Directory structure

```
SR110_Zephyr_camera/
|-- readme.md / readme_kr.md           (this document)
|-- 01_basic_capture/
|   |-- doc/
|   |   |-- LAB.md / LAB_kr.md         Lab writeup (English/Korean)
|   `-- lab/                            Buildable Zephyr application (CMakeLists.txt, prj.conf, src/, etc.)
|-- 02_continuous_stats/
|   |-- doc/{LAB.md, LAB_kr.md}
|   `-- lab/
|-- 03_tft_preview/
|   |-- doc/{LAB.md, LAB_kr.md, TROUBLESHOOTING.md, TROUBLESHOOTING_kr.md}
|   `-- lab/
|-- 04_jpeg_encode/
|   |-- doc/{LAB.md, LAB_kr.md}
|   `-- lab/
`-- 05_usb_stream/
    |-- doc/{LAB.md, LAB_kr.md, TROUBLESHOOTING.md, TROUBLESHOOTING_kr.md}
    `-- lab/
```

- **`doc/`**: the lab writeup, provided in both English (`LAB.md`) and Korean (`LAB_kr.md`). Labs where a real issue and fix came up during hardware verification (03, 05) also have `TROUBLESHOOTING.md`/`TROUBLESHOOTING_kr.md`.
- **`lab/`**: the actual Zephyr west application you build and flash. Each lab's "Step 1. Build & flash" section points `west build -s` at this folder.

## What you need (common)

- Synaptics Astra SR110 RDK board
- OV02C10 camera module (connected to the board's J23 connector)
- A west-CLI-based Zephyr dev environment (WSL2 or native PowerShell), OpenOCD + `openocd_flash.py`
- A serial terminal (PuTTY / TeraTerm / screen / minicom)
- Lab 03/04 only: an ST7789V3 TFT module + level shifter (e.g. TXS0108E), an external USB-TTL adapter
- Lab 05 only: a USB port on the PC that can be opened as serial, Python 3 + `pyserial`/`opencv-python`/`numpy`

Each lab's `doc/LAB.md` has a detailed "What you need" section covering that lab's specific parts and wiring.

## Getting started

1. Start with [`01_basic_capture/doc/LAB.md`](./01_basic_capture/doc/LAB.md) and work through the labs in order.
2. Each lab doc follows the same structure: "Goal -> What you should learn -> What you need -> Concept -> Step 1 (build/flash) -> Step 2 (run/verify) -> Verification checklist -> Next."
3. If you get stuck, check first whether that lab has a `TROUBLESHOOTING.md` (labs 03 and 05 do).

## Background

This repository was split out of an SR110 camera+AI integration curriculum, where these camera-fundamentals labs originally lived as part of a folder named `02_camera_capture`. Splitting them out lets "how to use the camera" be learned independently. The AI curriculum now treats this repository's labs as a prerequisite and focuses purely on combining the camera with the NPU.
