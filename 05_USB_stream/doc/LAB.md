# Lab 05. Real-Time Streaming to a PC over USB CDC — Synaptics Astra SR110 (sr100_rdk/sr100/m55)

## Goal of this lab

Through Lab 04, camera frames could only be checked on the board itself (a GDB dump, or a TFT still image). This lab **continuously streams the frames produced by the JPEG hardware encoder to a PC in real time over USB**, and a Python script on the PC displays that video like a live feed. It's a way to see "what the camera is looking at right now" in real time, with no physical display connected to the board at all.

> **Hardware verification: done.** Build, flash, USB CDC enumeration, and real-time streaming have all been confirmed working on real SR110 RDK hardware. Two issues hit during development — a flash-size overflow and a USB controller init failure — and their fixes are documented in [`TROUBLESHOOTING.md`](./TROUBLESHOOTING.md).

## What this lab investigates

- That SR110 enumerates as USB CDC ACM (a virtual COM port), so it can be opened on a PC like a regular serial port with no driver installation.
- Wiring up the full pipeline (capture → encode → USB transfer → receive → decode → display) end to end, where the board encodes JPEG every frame and sends it over USB, and the PC receives, decodes, and displays it.
- The "wait for DTR (virtual serial connection signal)" pattern that keeps the board from starting to stream before the host is ready.

## What you should learn

- **USB CDC ACM**: the standard USB class that emulates a virtual serial port over USB. The host OS recognizes it as `/dev/ttyACM0` (Linux/WSL) or `COMx` (Windows) with no extra driver.
- **Zephyr's `sample_usbd` helper**: the standard Zephyr sample utility (`samples/subsys/usb/common/`) that wraps USB bootstrap boilerplate — USB device stack (USBD) init, VID/PID setup, etc. — shared between this lab and the SDK's `mipi_capture_to_enc` sample.
- **Waiting for DTR (Data Terminal Ready)**: when the host opens the virtual COM port (e.g. `serial.Serial(...)` in Python), the DTR signal is asserted. The board waits for this signal before starting to stream, avoiding the situation where data is sent and dropped before the host-side viewer has even opened.
- **Framing (length/checksum header)**: since serial is a stream with no inherent boundaries, a 16-byte header carrying "how many bytes this frame is + its CRC32" must be prepended to every frame so the receiver can find frame boundaries precisely and detect corruption.
- **Interrupt-driven UART transmit**: sending via `uart_irq_tx_enable()`/`uart_fifo_fill()` so the CPU is notified of transmit completion by interrupt instead of busy-polling (waiting on a semaphore instead of a busy-wait).

## Good to know

- **How this lab differs from the SDK original (`mipi_capture_to_enc`)**: the original SDK sample captures a single FHD (1920x1080) frame, splits it into four 960x540 quadrants for encoding+transfer, and includes optional features up to XSPI flash storage and UVC (USB Video Class) transfer — a much more complex, capstone-level sample. This lab **keeps the single-480x270-frame structure** this curriculum has used all along (Lab 01-04), and doesn't include quadrant splitting, XSPI storage, or UVC — instead, it implements "real-time streaming" by sending that same 480x270 frame **repeatedly, in an infinite loop**. It also uses only one USB transport, CDC ACM (which also fits neatly with the WSL+`usbipd` workflow).
- **The USB CDC transport code (`usb_cdc_transport.c`/`.h`) is reused verbatim from the original, unchanged**: this file is a general-purpose "send a header + an arbitrarily sized JPEG buffer" function, so it never had any FHD- or quadrant-specific code to begin with.
- **`main.c` is Lab 04's / the original `enc` sample's one-shot capture flow turned into a repeating loop**: encoder setup → buffer queueing → stream start is identical, and after that it repeats "dequeue → send over USB → re-enqueue the same buffer" forever. So the host can turn the viewer off (close the port) and back on again, a failed send tears down the stream and goes back to waiting for DTR again.
- **mode=1 (reserved-memory color bars) is also supported as an optional exercise**, but since there's no camera in this mode it encodes the exact same still image every time — infinite streaming would be meaningless, so it's set to send 30 repeated frames and then stop (useful for confirming the USB CDC pipeline itself works with no camera).
- **Host Python script (`tools/recv_stream_cdc.py`)**: adapted from the SDK original's `recv_quadrants_cdc.py` (receives 4 frames and exits) into a form that "loops forever, continuously receiving and refreshing an OpenCV window." The header format (`QBUS` magic + version + frame ID + length + CRC32) is identical to the original, so the parsing logic is reused as-is. Press `q` to quit.
- **Required packages**: `pip install pyserial opencv-python numpy` (on the PC side — works with either WSL or native Windows Python).
- **The build requires the `-DCONFIG_XIP=n` option** — see [`TROUBLESHOOTING.md`](./TROUBLESHOOTING.md) for why.

## What you need

- SR110 RDK board (connected to the PC via USB cable — this needs the board's USB device port, separate from the debugger USB used in the camera labs)
- PC (Windows or WSL) — an environment that can open a serial port, Python 3 + `pyserial`/`opencv-python`/`numpy`

## Concept diagram

```
[Camera (ov02c10)] --CSI--> [JPEG encoder (lp_jpeg0)] --dequeue--> [main.c loop]
                                                                    |
                                                          cdc_transport_send_jpeg()
                                                                    |
                                                                    v
                                                        [USB CDC ACM virtual COM port]
                                                                    |
                                                                    v (USB cable)
                                                        [PC: recv_stream_cdc.py]
                                                          parse header -> decode JPEG
                                                          -> display live in an OpenCV window
```

## Step 1. Build & flash

`lab/` is ready to go. By default it streams infinitely in live sensor mode (`mode=0`, 480x270).

Since the flashed image overflows the FLASH region under the default linker settings, you must also pass **`-DCONFIG_XIP=n`** (run code from RAM instead of executing it directly from flash / XIP) — see item 1 of [`TROUBLESHOOTING.md`](./TROUBLESHOOTING.md) for why and for alternatives.

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <workspace>/SR110_Zephyr_camera/05_usb_stream/lab -d build_camera_usb_stream -- -DEXTRA_CONF_FILE=prj_cdc.conf -DCONFIG_XIP=n
```

(Optional) If you want to check just the USB CDC pipeline with no camera, build with the reserved-memory color-bar mode (`mode=1`):

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <workspace>/SR110_Zephyr_camera/05_usb_stream/lab -d build_camera_usb_stream_mem -- -DEXTRA_CONF_FILE=prj_cdc.conf -DCONFIG_XIP=n -DDTS_EXTRA_CPPFLAGS="-DENC_MEMORY_INPUT"
```

Flash the combined image with `openocd_flash.py` as usual.

## Step 2. Run & verify

1. Resetting/powering on the board produces a log like this on the console (from an actual measured run):
   ```
   *** Booting Zephyr OS build v4.4.1 ***
   [00:00:00.360,000] <inf> usb_stream_sample: USB CDC streaming lab start (live-to-memory, mode=0)
   [00:00:00.365,000] <inf> usb_stream_sample: enc video_set_format ret=0 capacity=131072 pitch=0 (480x270)
   [00:00:00.370,000] <inf> usb_stream_sample: enc video_get_caps ret=0 min_vbuf=1 align=64
   [00:00:00.374,000] <inf> usb_stream_sample: enc app_buf[0] addr=0x... size=131072 align=64
   [00:00:00.379,000] <inf> usb_stream_sample: enc video_enqueue[0] ret=0
   [00:00:00.383,000] <inf> usb_stream_sample: enc app_buf[1] addr=0x... size=131072 align=64
   [00:00:00.387,000] <inf> usb_stream_sample: enc video_enqueue[1] ret=0
   [00:00:00.405,000] <inf> usb_stream_sample: enc video_stream_start ret=0
   USB CDC ready; waiting for host DTR (connect + open the PC viewer)...
   ```
2. Connecting the board to the PC via USB cable enumerates a new virtual COM port (check "Syna CDC ACM" in Windows Device Manager, or bind it with `usbipd` and check `/dev/ttyACM0` on WSL — reusing the USB/WSL procedure from Lab 01's doc).
3. Run the viewer script on the PC:
   ```bash
   python recv_stream_cdc.py --port COM5        # Windows example
   # or
   python recv_stream_cdc.py --port /dev/ttyACM0   # WSL example
   ```
4. Once the viewer opens the port (i.e. DTR is asserted), the board console should print `Host DTR set - streaming started (mode=live-to-memory)`, and the PC screen should start updating in real time with what the camera sees (on the order of tens of fps, much faster than the TFT preview — because it's USB, not SPI).
5. Press `q` to quit the viewer. Running the viewer again (reopening the port) makes the board automatically start streaming again.

## Verification checklist

- [x] `west build -b sr100_rdk/sr100/m55` builds successfully (mode=0, `EXTRA_CONF_FILE=prj_cdc.conf`, `-DCONFIG_XIP=n`)
- [x] The board enumerates correctly as USB CDC ACM (virtual COM port), visible on the PC
- [x] Running `recv_stream_cdc.py` prints `Host DTR set - streaming started` on the board console
- [x] The camera feed updates continuously (no stalling) on the PC screen
- [x] Streaming correctly resumes after closing and reopening the viewer (closing/reopening the port)
- [ ] (Optional) Also build with `mode=1` (reserved-memory color-bar pattern) to confirm the USB pipeline itself works with no camera (auto-stops after 30 frames)

## Next

This wraps up the camera-fundamentals lab series (Lab 01-05). Camera-based AI labs (NPU inference, etc.) continue in a separate curriculum.

See [`TROUBLESHOOTING.md`](./TROUBLESHOOTING.md) for the issues hit during build/run and how they were resolved.
