/*
 * Lab C4 (Camera JPEG Encode + TFT Still Preview).
 *
 * The low-level ST7789V3 init sequence, SPI0 8-byte FIFO chunking
 * (st7789_send()) and addr-window setup below are taken verbatim from
 * ai_labs/03_sensor_anomaly's tft_status.c / Lab C3's tft_preview.c -
 * see those files for the extended commentary on why a raw driver is
 * used instead of Zephyr's in-tree "sitronix,st7789v" driver, and why
 * SPI0 writes must be chunked to 8 bytes.
 *
 * What is new here is tft_still_show_jpeg(): instead of drawing text
 * (03_sensor_anomaly) or a continuously-refreshed grayscale
 * debayer-lite preview (Lab C3), this decodes the still JPEG the
 * hardware encoder just produced - using the vendored TJpgDec decoder
 * (tjpgd.c, see its header for license/version) - and displays it
 * once as a real color image.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <stdbool.h>

#include "tft_still.h"
#include "tjpgd.h"

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

void tft_still_init(void)
{
	int ret;

	if (!spi_is_ready_dt(&spi_spec) || !gpio_is_ready_dt(&reset_spec) ||
	    !gpio_is_ready_dt(&dc_spec)) {
		printk("tft_still: SPI/GPIO not ready, display disabled\n");
		return;
	}

	ret = gpio_pin_configure_dt(&dc_spec, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		printk("tft_still: dc-gpio configure failed: %d, display disabled\n", ret);
		return;
	}

	if (st7789_reset() || st7789_init()) {
		printk("tft_still: reset/init failed (check wiring/level shifter), display disabled\n");
		return;
	}
	st7789_fill_rect(0, 0, PANEL_WIDTH, PANEL_HEIGHT, COLOR_BLACK);
	tft_ready = true;
}

/*
 * ---- JPEG decode ----
 *
 * This lab's live capture is fixed at 480x270 (see APP_LIVE_WIDTH/
 * APP_LIVE_HEIGHT in main.c). TJpgDec is asked to decode at scale=1
 * (see tjpgdcnf.h: JD_USE_SCALE=1), which halves both dimensions
 * *during* decode (cheaper than decoding full-size and downscaling
 * afterwards) - so the decoded image lands directly at 240x135,
 * already close to the panel's resolution.
 */
#define DEC_WIDTH  240U
#define DEC_HEIGHT 135U
#define JD_SCALE   1U /* 1/2 size: matches DEC_WIDTH/DEC_HEIGHT above */

/* TJpgDec's own required workspace for Huffman/IDCT tables etc. -
 * ChaN's documentation suggests ~3.1 KB is enough for most images;
 * this leaves some headroom. If jd_prepare()/jd_decomp() ever returns
 * JDR_MEM1, raise this.
 */
#define JD_WORK_SIZE 4096U

static uint8_t jd_work[JD_WORK_SIZE];
/* Decoded RGB565 framebuffer, one uint16_t per pixel. */
static uint16_t g_decode_fb[DEC_WIDTH * DEC_HEIGHT];

struct jpeg_input_ctx {
	const uint8_t *data;
	size_t size;
	size_t pos;
};

/* TJpgDec input callback: pulls up to ndata bytes from our in-memory
 * JPEG buffer. buf may be NULL (TJpgDec asking to skip bytes without
 * reading them) - memcpy is skipped in that case.
 */
static size_t jpeg_input_func(JDEC *jd, uint8_t *buf, size_t ndata)
{
	struct jpeg_input_ctx *ctx = (struct jpeg_input_ctx *)jd->device;
	size_t avail = ctx->size - ctx->pos;
	size_t n = MIN(ndata, avail);

	if (buf != NULL && n != 0U) {
		memcpy(buf, ctx->data + ctx->pos, n);
	}
	ctx->pos += n;
	return n;
}

/* TJpgDec output callback: called once per decoded MCU block with a
 * packed RGB565 bitmap (JD_FORMAT=1, see tjpgdcnf.h) for that block's
 * rectangle. We just copy it into our full-frame buffer at the right
 * offset; the actual panel transform happens after decode is done.
 */
static int jpeg_output_func(JDEC *jd, void *bitmap, JRECT *rect)
{
	ARG_UNUSED(jd);

	const uint16_t *src = (const uint16_t *)bitmap;
	uint16_t block_w = (uint16_t)(rect->right - rect->left + 1);
	uint16_t block_h = (uint16_t)(rect->bottom - rect->top + 1);

	for (uint16_t y = 0; y < block_h; y++) {
		uint16_t dst_row = (uint16_t)(rect->top + y);

		if (dst_row >= DEC_HEIGHT) {
			continue; /* Bottom padding row beyond our framebuffer - ignore. */
		}
		for (uint16_t x = 0; x < block_w; x++) {
			uint16_t dst_col = (uint16_t)(rect->left + x);

			if (dst_col >= DEC_WIDTH) {
				continue; /* Right padding column - ignore. */
			}
			g_decode_fb[(size_t)dst_row * DEC_WIDTH + dst_col] = src[(size_t)y * block_w + x];
		}
	}
	return 1; /* Continue decoding. */
}

/*
 * ---- Rotate + "cover" crop (same geometry as Lab C3) ----
 *
 * The decoded frame (240x135, landscape) and the TFT panel (240x280,
 * portrait) do not share an aspect ratio, so exactly the same
 * rotate-then-cover-crop approach from Lab C3 is reused here - just
 * without the Bayer-block averaging step, since JPEG decoding already
 * produced full, real RGB565 pixels (this still image is shown in
 * actual color, unlike Lab C3's grayscale debayer-lite preview).
 */
static void compute_cover_crop(uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
				uint32_t *crop_x0, uint32_t *crop_y0,
				uint32_t *crop_w, uint32_t *crop_h)
{
	if (src_w * dst_h > src_h * dst_w) {
		*crop_h = src_h;
		*crop_w = src_h * dst_w / dst_h;
	} else {
		*crop_w = src_w;
		*crop_h = src_w * dst_h / dst_w;
	}
	*crop_x0 = (src_w - *crop_w) / 2U;
	*crop_y0 = (src_h - *crop_h) / 2U;
}

/* Maps a pixel in the rotated+cropped view back to its source pixel
 * in the decoded (unrotated) framebuffer. Inverse of a 90 degree
 * clockwise rotation, same as Lab C3's rotate_to_bayer_coords() minus
 * the 2x2 Bayer-grid snapping (not needed - every decoded pixel is
 * already a complete RGB565 value).
 */
static void rotate_to_fb_coords(uint32_t rot_col, uint32_t rot_row, uint32_t *fb_col,
				 uint32_t *fb_row)
{
	uint32_t row_s = (DEC_HEIGHT - 1U) - rot_col;
	uint32_t col_s = rot_row;

	if (row_s >= DEC_HEIGHT) {
		row_s = DEC_HEIGHT - 1U;
	}
	if (col_s >= DEC_WIDTH) {
		col_s = DEC_WIDTH - 1U;
	}

	*fb_col = col_s;
	*fb_row = row_s;
}

static void render_decoded_frame(void)
{
	/* Rotated canvas: decoded frame's width/height swapped. */
	uint32_t rotated_w = DEC_HEIGHT;
	uint32_t rotated_h = DEC_WIDTH;
	uint32_t crop_x0, crop_y0, crop_w, crop_h;
	uint8_t row[PANEL_WIDTH * 2];

	compute_cover_crop(rotated_w, rotated_h, PANEL_WIDTH, PANEL_HEIGHT,
			    &crop_x0, &crop_y0, &crop_w, &crop_h);

	if (st7789_set_addr_window(0, 0, PANEL_WIDTH - 1, PANEL_HEIGHT - 1)) {
		return;
	}

	for (uint16_t oy = 0; oy < PANEL_HEIGHT; oy++) {
		uint32_t rot_row = crop_y0 + (uint32_t)oy * crop_h / PANEL_HEIGHT;

		for (uint16_t ox = 0; ox < PANEL_WIDTH; ox++) {
			uint32_t rot_col = crop_x0 + (uint32_t)ox * crop_w / PANEL_WIDTH;
			uint32_t fx, fy;

			rotate_to_fb_coords(rot_col, rot_row, &fx, &fy);

			uint16_t pixel = g_decode_fb[(size_t)fy * DEC_WIDTH + fx];

			row[ox * 2U] = pixel >> 8;
			row[ox * 2U + 1U] = pixel & 0xFF;
		}

		if (st7789_write_data(row, (size_t)PANEL_WIDTH * 2U)) {
			return;
		}
	}
}

int tft_still_show_jpeg(const uint8_t *jpeg, size_t jpeg_size)
{
	struct jpeg_input_ctx ctx = { .data = jpeg, .size = jpeg_size, .pos = 0 };
	JDEC jd;
	JRESULT jr;

	if (!tft_ready) {
		return -ENODEV;
	}

	jr = jd_prepare(&jd, jpeg_input_func, jd_work, sizeof(jd_work), &ctx);
	if (jr != JDR_OK) {
		printk("tft_still: jd_prepare failed (JRESULT=%d)\n", jr);
		return -(int)jr;
	}

	if (jd.width != (DEC_WIDTH << JD_SCALE) || jd.height != (DEC_HEIGHT << JD_SCALE)) {
		printk("tft_still: unexpected JPEG size %ux%u (expected %ux%u) - showing anyway\n",
		       jd.width, jd.height, DEC_WIDTH << JD_SCALE, DEC_HEIGHT << JD_SCALE);
	}

	memset(g_decode_fb, 0, sizeof(g_decode_fb));

	jr = jd_decomp(&jd, jpeg_output_func, (uint8_t)JD_SCALE);
	if (jr != JDR_OK) {
		printk("tft_still: jd_decomp failed (JRESULT=%d)\n", jr);
		return -(int)jr;
	}

	render_decoded_frame();
	printk("tft_still: JPEG decoded (%ux%u) and shown on panel\n", jd.width, jd.height);
	return 0;
}
