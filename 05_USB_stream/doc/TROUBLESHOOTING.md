# Lab 05 Troubleshooting — Real-Time Streaming to a PC over USB CDC

Two issues hit during hardware verification and how they were resolved.

## 1. FLASH region overflow at build time

**Symptom**: Building with the default build options fails at the link stage with an error like:

```
.../ld.bfd: region `FLASH' overflowed by 11688 bytes
collect2: error: ld returned 1 exit status
ninja: build stopped: subcommand failed.
```

**Cause**: This lab links together the JPEG encoder code (Lab 04) + the USB device stack (USBD) + the CDC ACM class + an interrupt-driven UART driver, all at once. It has the largest code/symbol footprint of any camera lab so far, and under the default settings (XIP — executing code directly from flash) that footprint exceeds SR110's FLASH execute-region budget. This is the same class of problem Lab 03 hit when adding its shell feature (see [`03_tft_preview/doc/TROUBLESHOOTING.md`](../../03_tft_preview/doc/TROUBLESHOOTING.md)), but the feature responsible here (the USB stack) is large enough that a narrow config tweak like `CONFIG_SHELL_MINIMAL` alone wasn't enough to fix it.

**Fix**: Add `-DCONFIG_XIP=n` to the build command. This switches from executing code in place from flash to copying it into RAM at boot and running it from there, so the size is now measured against "RAM capacity" instead of "executable flash region" — RAM has enough headroom for this lab's code size, which resolves it.

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <workspace>/SR110_Zephyr_camera/05_usb_stream/lab -d build_camera_usb_stream -- -DEXTRA_CONF_FILE=prj_cdc.conf -DCONFIG_XIP=n
```

> Note: turning off XIP adds a step at boot that copies code from flash into RAM, so boot time increases slightly and RAM usage goes up. That worked fine for this lab, but if you'd rather keep XIP on and shrink code size instead, tuning `CONFIG_USBD_CDC_ACM_CLASS`/USB-stack-related Kconfig more finely, or trimming unneeded logging/features as Lab 03 did, are both viable alternatives.

## 2. USB controller init failure at runtime (`udc_dwc2: Wait for AHB idle timeout`)

**Symptom**: After fixing issue 1 above with `-DCONFIG_XIP=n` and reflashing, the very first run hit this runtime error:

```
*** Booting Zephyr OS build v4.4.1 ***
[00:00:00.360,000] <inf> usb_stream_sample: USB CDC streaming lab start (live-to-memory, mode=0)
[00:00:00.365,000] <inf> usb_stream_sample: enc video_set_format ret=0 capacity=131072 pitch=0 (480x270)
[00:00:00.370,000] <inf> usb_stream_sample: enc video_get_caps ret=0 min_vbuf=1 align=64
[00:00:00.374,000] <inf> usb_stream_sample: enc app_buf[0] addr=0x33ec4f80 size=131072 align=64
[00:00:00.379,000] <inf> usb_stream_sample: enc video_enqueue[0] ret=0
[00:00:00.383,000] <inf> usb_stream_sample: enc app_buf[1] addr=0x33ee4f80 size=131072 align=64
[00:00:00.387,000] <inf> usb_stream_sample: enc video_enqueue[1] ret=0
[00:00:00.405,000] <inf> usb_stream_sample: enc video_stream_start ret=0
[00:00:00.421,000] <err> udc_dwc2: Wait for AHB idle timeout, GRSTCTL 0x00000000
[00:00:00.425,000] <err> usbd_dev: Failed to enable controller
[00:00:00.429,000] <err> usb_stream_sample: USB CDC init failed: -5
[00:00:00.433,000] <inf> video_syna_sr100_enc: JPEG encoder stream stopped
[00:00:00.437,000] <inf> usb_stream_sample: USB CDC streaming lab finished ret=-5 after 77 ms
```

On the next retry, USB CDC ACM initialized normally and streaming worked correctly.

**Cause**: The USB DWC2 controller timed out (`GRSTCTL 0x00000000`) waiting for the AHB (internal bus) to go idle after reset — a hardware-initialization-timing issue with the USB controller. It's possible that the boot procedure change from `-DCONFIG_XIP=n` (copying code to RAM) shifted init timing enough to be related to this one-time failure, but the exact root cause was not conclusively identified — it appears the USB controller reset timing right after power-on can occasionally be tight enough to fail, and this did not reproduce on any subsequent retry.

**Fix (if you hit this)**: Reset the board or power-cycle it and try again — retrying was confirmed to initialize correctly, and repeated testing after that did not reproduce the issue. If this error reproduces every time for you, it's also worth trying a different USB cable/port, or changing the power-on sequence (order of external power vs. USB cable connection).
