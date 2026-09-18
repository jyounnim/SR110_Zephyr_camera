/* Lab C3 TFT preview helper, built on the raw-SPI ST7789V3 driver
 * confirmed working in ai_labs/03_sensor_anomaly. Unlike that lab's
 * tft_status.c (which only draws text), this one pushes an actual
 * downscaled grayscale camera frame to the panel.
 */
#ifndef TFT_PREVIEW_H_
#define TFT_PREVIEW_H_

#include <stddef.h>
#include <stdint.h>

/* Call once at boot. Safe to call even if the panel/level-shifter
 * isn't connected - failures are logged via printk and
 * tft_preview_show_frame() is simply left a no-op.
 */
void tft_preview_init(void);

/* Rotates a RAW8 Bayer (SRGGB8) frame 90 degrees to match the panel's
 * portrait orientation, "cover" crops it to the panel's exact aspect
 * ratio, and pushes it as a grayscale image that fills the whole
 * panel (no blank margins) - see the implementation comment in
 * tft_preview.c for the full geometry. src_width/src_height must both
 * be even (WQVGA 480x270 satisfies this). No-op if tft_preview_init()
 * didn't succeed.
 */
void tft_preview_show_frame(const uint8_t *bayer, uint16_t src_width, uint16_t src_height);

#endif /* TFT_PREVIEW_H_ */
