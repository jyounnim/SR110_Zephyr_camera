/* Lab C4 TFT still-preview helper, built on the same raw-SPI
 * ST7789V3 driver confirmed working in ai_labs/03_sensor_anomaly and
 * Lab C3. Unlike Lab C3 (which pushed a continuous grayscale
 * debayer-lite preview), this pushes a single still image decoded
 * from the JPEG the hardware encoder just produced - a real color
 * image, since JPEG decoding does full chroma reconstruction.
 */
#ifndef TFT_STILL_H_
#define TFT_STILL_H_

#include <stddef.h>
#include <stdint.h>

/* Call once at boot. Safe to call even if the panel/level-shifter
 * isn't connected - failures are logged via printk and
 * tft_still_show_jpeg() is simply left a no-op.
 */
void tft_still_init(void);

/* Decodes a JPEG buffer (as produced by the syna,enc-video hardware
 * encoder) and displays it as a single still image, filling the
 * whole panel (rotated 90 degrees + "cover" cropped exactly like Lab
 * C3 - see the implementation comment in tft_still.c). Expects a
 * 480x270 source JPEG (this lab's fixed capture resolution); returns
 * 0 on success, a negative TJpgDec error code on decode failure, or
 * -ENODEV if tft_still_init() didn't succeed. No-op (return value
 * still meaningful) if the panel isn't ready.
 */
int tft_still_show_jpeg(const uint8_t *jpeg, size_t jpeg_size);

#endif /* TFT_STILL_H_ */
