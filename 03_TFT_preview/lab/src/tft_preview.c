/*
 * Lab C3 (Camera TFT Preview) - raw-SPI ST7789V3 driver.
 *
 * The init sequence, SPI0 8-byte FIFO chunking (st7789_send()) and
 * addr-window setup below are taken verbatim from
 * ai_labs/03_sensor_anomaly's tft_status.c - see that file for the
 * extended commentary on why a raw driver is used instead of
 * Zephyr's in-tree "sitronix,st7789v" driver, and why SPI0 writes
 * must be chunked to 8 bytes. Only tft_preview_show_frame() is new:
 * instead of drawing text, it pushes a downscaled camera frame.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <stdbool.h>

#include "tft_preview.h"

#define DISP_NODE DT_NODELABEL(st7789v_disp)

#define PANEL_WIDTH   DT_PROP(DISP_NODE, width)
#define PANEL_HEIGHT  DT_PROP(DISP_NODE, height)
#define X_OFFSET      DT_PROP(DISP_NODE, x_offset)
#define Y_OFFSET      DT_PROP(DISP_NODE, y_offset)

#define ST7789_SWRESET  0x01
#define ST7789_SLPOUT   0x11
#define ST7789_COLMOD   0x3A
#define ST7789_MADCTL   0x36
#define ST7789_INVON    0x21
#define ST7789_NORON    0x13
#define ST7789_DISPON   0x29
#define ST7789_CASET    0x2A
#define ST7789_RASET    0x2B
#define ST7789_RAMWR    0x2C
#define ST7789_PORCTRL  0xB2
#define ST7789_GCTRL    0xB7
#define ST7789_VCOMS    0xBB
#define ST7789_LCMCTRL  0xC0
#define ST7789_VDVVRHEN 0xC2
#define ST7789_VRHS     0xC3
#define ST7789_VDVS     0xC4
#define ST7789_FRCTRL2  0xC6
#define ST7789_PWCTRL1  0xD0
#define ST7789_PVGAMCTRL 0xE0
#define ST7789_NVGAMCTRL 0xE1

#define COLOR_BLACK   0x0000

#define ST7789_CHUNK_BYTES 8

static const struct spi_dt_spec spi_spec =
	SPI_DT_SPEC_GET(DISP_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);
static const struct gpio_dt_spec reset_spec = GPIO_DT_SPEC_GET(DISP_NODE, reset_gpios);
static const struct gpio_dt_spec dc_spec = GPIO_DT_SPEC_GET(DISP_NODE, dc_gpios);

static bool tft_ready;

static int st7789_send(int dc_value, const uint8_t *data, size_t len)
{
	int ret;

	gpio_pin_set_dt(&dc_spec, dc_value);

	while (len) {
		size_t chunk = MIN(len, ST7789_CHUNK_BYTES);
		struct spi_buf buf = { .buf = (void *)data, .len = chunk };
		struct spi_buf_set set = { .buffers = &buf, .count = 1 };

		ret = spi_write_dt(&spi_spec, &set);
		if (ret) {
			return ret;
		}
		data += chunk;
		len -= chunk;
	}
	return 0;
}

static int st7789_write_cmd(uint8_t cmd)
{
	return st7789_send(0, &cmd, 1);
}

static int st7789_write_data(const uint8_t *data, size_t len)
{
	return st7789_send(1, data, len);
}

static int st7789_reset(void)
{
	int ret = gpio_pin_configure_dt(&reset_spec, GPIO_OUTPUT_INACTIVE);

	if (ret) {
		return ret;
	}
	k_sleep(K_MSEC(10));
	gpio_pin_set_dt(&reset_spec, 1);
	k_sleep(K_MSEC(10));
	gpio_pin_set_dt(&reset_spec, 0);
	k_sleep(K_MSEC(150));
	return 0;
}

struct st7789_init_cmd {
	uint8_t cmd;
	uint8_t num_args;
	uint8_t args[16];
	uint16_t delay_ms;
};

static const struct st7789_init_cmd init_seq[] = {
	{ ST7789_SWRESET,   0, {0}, 150 },
	{ ST7789_SLPOUT,    0, {0}, 255 },
	{ ST7789_COLMOD,    1, {0x55}, 10 },
	{ ST7789_PORCTRL,   5, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 0 },
	{ ST7789_GCTRL,     1, {0x35}, 0 },
	{ ST7789_VCOMS,     1, {0x28}, 0 },
	{ ST7789_LCMCTRL,   1, {0x0C}, 0 },
	{ ST7789_VDVVRHEN,  2, {0x01, 0xFF}, 0 },
	{ ST7789_VRHS,      1, {0x10}, 0 },
	{ ST7789_VDVS,      1, {0x20}, 0 },
	{ ST7789_FRCTRL2,   1, {0x0F}, 0 },
	{ ST7789_PWCTRL1,   2, {0xA4, 0xA1}, 0 },
	{ ST7789_MADCTL,    1, {0x08}, 0 },  /* 0x08 = BGR order (this panel; 0x00 is RGB) - see ai_labs/03_sensor_anomaly */
	{ ST7789_INVON,     0, {0}, 10 },
	{ ST7789_PVGAMCTRL, 14, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44,
				 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17}, 0 },
	{ ST7789_NVGAMCTRL, 14, {0xD0, 0x00, 0x02, 0x07, 0x05, 0x25, 0x2D, 0x44,
				 0x45, 0x10, 0x0E, 0x12, 0x13, 0x17}, 0 },
	{ ST7789_NORON,     0, {0}, 10 },
	{ ST7789_DISPON,    0, {0}, 100 },
};

static int st7789_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(init_seq); i++) {
		int ret = st7789_write_cmd(init_seq[i].cmd);

		if (ret) return ret;
		if (init_seq[i].num_args) {
			ret = st7789_write_data(init_seq[i].args, init_seq[i].num_args);
			if (ret) return ret;
		}
		if (init_seq[i].delay_ms) {
			k_sleep(K_MSEC(init_seq[i].delay_ms));
		}
	}
	return 0;
}

static int st7789_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint16_t xs = x0 + X_OFFSET, xe = x1 + X_OFFSET;
	uint16_t ys = y0 + Y_OFFSET, ye = y1 + Y_OFFSET;
	uint8_t caset[4] = { xs >> 8, xs & 0xFF, xe >> 8, xe & 0xFF };
	uint8_t raset[4] = { ys >> 8, ys & 0xFF, ye >> 8, ye & 0xFF };
	int ret;

	ret = st7789_write_cmd(ST7789_CASET);
	if (ret) return ret;
	ret = st7789_write_data(caset, sizeof(caset));
	if (ret) return ret;
	ret = st7789_write_cmd(ST7789_RASET);
	if (ret) return ret;
	ret = st7789_write_data(raset, sizeof(raset));
	if (ret) return ret;
	return st7789_write_cmd(ST7789_RAMWR);
}

static int st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
	uint8_t row[PANEL_WIDTH * 2];
	int ret;

	ret = st7789_set_addr_window(x, y, x + w - 1, y + h - 1);
	if (ret) return ret;

	for (uint16_t i = 0; i < w; i++) {
		row[i * 2] = color >> 8;
		row[i * 2 + 1] = color & 0xFF;
	}
	for (uint16_t line = 0; line < h; line++) {
		ret = st7789_write_data(row, (size_t)w * 2);
		if (ret) return ret;
	}
	return 0;
}

void tft_preview_init(void)
{
	int ret;

	if (!spi_is_ready_dt(&spi_spec) || !gpio_is_ready_dt(&reset_spec) ||
	    !gpio_is_ready_dt(&dc_spec)) {
		printk("tft_preview: SPI/GPIO not ready, display disabled\n");
		return;
	}

	ret = gpio_pin_configure_dt(&dc_spec, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		printk("tft_preview: dc-gpio configure failed: %d, display disabled\n", ret);
		return;
	}

	if (st7789_reset() || st7789_init()) {
		printk("tft_preview: reset/init failed (check wiring/level shifter), display disabled\n");
		return;
	}
	st7789_fill_rect(0, 0, PANEL_WIDTH, PANEL_HEIGHT, COLOR_BLACK);
	tft_ready = true;
}

/*
 * This lab's camera frame (480x270, landscape) and the TFT panel
 * (240x280, portrait) do not share an aspect ratio, so no crop-free
 * mapping can fill the whole panel. Three things happen in one pass
 * per output pixel:
 *
 *   1. Rotate 90 degrees: the sensor's long axis (480, its width) is
 *      matched up with the panel's long axis (280, its height)
 *      instead of its short axis (240) - this keeps far more of the
 *      original field of view than displaying unrotated ever could,
 *      since a landscape sensor naturally suits a landscape framing
 *      much better than a portrait one.
 *   2. "Cover" crop + nearest-neighbor scale: after the conceptual
 *      rotation, the image (270x480) still is not exactly 240x280,
 *      so it is scaled by the single factor that makes it *at least*
 *      as big as the panel in both directions, and the extra pixels
 *      on the now-oversized axis are cropped off both edges evenly
 *      (that scale/crop math lives in compute_cover_crop() below) -
 *      the same idea a phone camera app uses for "fill" photo crops,
 *      just done with integer nearest-neighbor instead of
 *      interpolation, which is more than good enough at this size.
 *   3. Debayer-lite: same 2x2 Bayer block average as before, sampled
 *      at the block the rotate+crop math points to, giving one
 *      grayscale value per output pixel.
 *
 * If your particular panel/camera pairing needs the image mirrored or
 * rotated the other way, that only changes the algebra in
 * rotate_to_bayer_coords() below - the crop math itself does not care
 * which way the rotation goes.
 */

/* Given a source rectangle (src_w x src_h) and a destination rectangle
 * (dst_w x dst_h) with a different aspect ratio, compute the single
 * uniform scale factor and centered crop window in the SOURCE that,
 * once scaled up to fill dst_w x dst_h, leaves no blank space (the
 * "cover" behavior familiar from CSS background-size:cover or a photo
 * app's "fill" crop). Returns the crop window's origin and size, still
 * in source pixels - the caller does the actual scaling by mapping
 * each destination pixel back into this window with plain division.
 */
static void compute_cover_crop(uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
				uint32_t *crop_x0, uint32_t *crop_y0,
				uint32_t *crop_w, uint32_t *crop_h)
{
	if (src_w * dst_h > src_h * dst_w) {
		/* Source is relatively wider than the destination: use the
		 * full source height and crop its width.
		 */
		*crop_h = src_h;
		*crop_w = src_h * dst_w / dst_h;
	} else {
		/* Source is relatively taller than the destination: use the
		 * full source width and crop its height.
		 */
		*crop_w = src_w;
		*crop_h = src_w * dst_h / dst_w;
	}
	*crop_x0 = (src_w - *crop_w) / 2U;
	*crop_y0 = (src_h - *crop_h) / 2U;
}

/* Maps a pixel in the rotated+cropped view back to the top-left corner
 * of a 2x2 Bayer block in the original, unrotated sensor frame. See
 * the big comment above tft_preview_show_frame() for the geometry.
 */
static void rotate_to_bayer_coords(uint32_t rot_col, uint32_t rot_row, uint32_t src_width,
				    uint32_t src_height, uint32_t *bayer_col,
				    uint32_t *bayer_row)
{
	/* Inverse of a 90 degree clockwise rotation: original row
	 * = (src_height-1) - rotated_col, original col = rotated_row.
	 * rot_col is always within [0, src_height) and rot_row within
	 * [0, src_width) by construction (they come from a crop window
	 * sized against the rotated canvas, see tft_preview_show_frame()),
	 * so both subtractions below stay non-negative.
	 */
	uint32_t row_s = (src_height - 1U) - rot_col;
	uint32_t col_s = rot_row;

	/* Round down to an even (row, col) so the 2x2 Bayer block we read
	 * from here always starts on the same R/G/G/B grid as the sensor,
	 * then clamp so the block's second row/column stays in bounds.
	 */
	row_s &= ~1U;
	col_s &= ~1U;
	if (row_s > src_height - 2U) {
		row_s = (src_height - 2U) & ~1U;
	}
	if (col_s > src_width - 2U) {
		col_s = (src_width - 2U) & ~1U;
	}

	*bayer_col = col_s;
	*bayer_row = row_s;
}

void tft_preview_show_frame(const uint8_t *bayer, uint16_t src_width, uint16_t src_height)
{
	/* The "rotated canvas" is what the sensor frame looks like after a
	 * 90 degree turn: its width is the sensor's height and vice versa.
	 */
	uint32_t rotated_w = src_height;
	uint32_t rotated_h = src_width;
	uint32_t crop_x0, crop_y0, crop_w, crop_h;
	uint8_t row[PANEL_WIDTH * 2];

	if (!tft_ready) {
		return;
	}

	compute_cover_crop(rotated_w, rotated_h, PANEL_WIDTH, PANEL_HEIGHT,
			    &crop_x0, &crop_y0, &crop_w, &crop_h);

	if (st7789_set_addr_window(0, 0, PANEL_WIDTH - 1, PANEL_HEIGHT - 1)) {
		return;
	}

	for (uint16_t oy = 0; oy < PANEL_HEIGHT; oy++) {
		uint32_t rot_row = crop_y0 + (uint32_t)oy * crop_h / PANEL_HEIGHT;

		for (uint16_t ox = 0; ox < PANEL_WIDTH; ox++) {
			uint32_t rot_col = crop_x0 + (uint32_t)ox * crop_w / PANEL_WIDTH;
			uint32_t bx, by;

			rotate_to_bayer_coords(rot_col, rot_row, src_width, src_height, &bx, &by);

			const uint8_t *row0 = bayer + (size_t)by * src_width + bx;
			const uint8_t *row1 = row0 + src_width;
			uint16_t sum = row0[0] + row0[1] + row1[0] + row1[1];
			uint8_t gray = (uint8_t)(sum / 4U);
			/* RGB565 with R=G=B gives a neutral gray pixel. */
			uint16_t gray5 = gray >> 3;
			uint16_t gray6 = gray >> 2;
			uint16_t pixel = (gray5 << 11) | (gray6 << 5) | gray5;

			row[ox * 2U] = pixel >> 8;
			row[ox * 2U + 1U] = pixel & 0xFF;
		}

		if (st7789_write_data(row, (size_t)PANEL_WIDTH * 2U)) {
			return;
		}
	}
}
