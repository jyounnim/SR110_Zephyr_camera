/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SR110 AI curriculum Lab C3 - Camera capture + TFT preview.
 *
 * Builds on lab_c1_basic_capture / lab_c2_continuous_stats's video_api
 * capture loop (same devicetree node, same WQVGA RAW8 format). Instead
 * of logging stats, each captured frame is downscaled to grayscale and
 * pushed to the ST7789V3 TFT (SPI0) via tft_preview_show_frame() - see
 * that file for how the downscale + grayscale conversion works.
 *
 * The camera stream is started once and left running; the main loop
 * only pulls one frame out of it every g_preview_interval_ms to
 * display, so the panel updates a few times per second at most instead
 * of at the camera's native ~30fps (SPI0 is far too slow for that
 * anyway, and a very short interval also floods the serial console
 * with one log line per frame). The interval is a plain runtime
 * variable (not a compile-time constant) so it can be changed on the
 * fly from the shell (see cmd_preview_interval_set() below) without
 * rebuilding/reflashing - handy for finding a comfortable refresh rate
 * by trial and error.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/video/video.h>

#include "tft_preview.h"

LOG_MODULE_REGISTER(camera_tft_preview, CONFIG_LOG_DEFAULT_LEVEL);

#define VIDEO_SAMPLE_FRAME_WIDTH      ((uint32_t)CONFIG_VIDEO_SAMPLE_FRAME_WIDTH)
#define VIDEO_SAMPLE_FRAME_HEIGHT     ((uint32_t)CONFIG_VIDEO_SAMPLE_FRAME_HEIGHT)
#define VIDEO_SAMPLE_FRAME_PITCH      ((uint32_t)CONFIG_VIDEO_SAMPLE_FRAME_PITCH)
#define VIDEO_SAMPLE_BUFFER_COUNT     ((uint32_t)CONFIG_VIDEO_SAMPLE_BUFFER_COUNT)
#define VIDEO_SAMPLE_CAPTURE_TIMEOUT  K_MSEC(CONFIG_VIDEO_SAMPLE_CAPTURE_TIMEOUT_MS)

#define PREVIEW_INTERVAL_MIN_MS 100
#define PREVIEW_INTERVAL_MAX_MS 10000

#define VIDEO_SAMPLE_PIXEL_FORMAT     VIDEO_PIX_FMT_SRGGB8

#define VIDEO_DEV_NODE DT_NODELABEL(video_syna0)

#if !DT_NODE_EXISTS(VIDEO_DEV_NODE)
#error "Devicetree node 'video_syna0' is required for this sample"
#endif

/* Boot default comes from Kconfig; from then on this is the only
 * source of truth, changed live by the "preview interval set <ms>"
 * shell command. atomic_t (not a plain uint32_t) because it is
 * written from the shell thread and read from the main loop thread.
 */
static atomic_t g_preview_interval_ms = ATOMIC_INIT(CONFIG_TFT_PREVIEW_INTERVAL_MS);

static int cmd_preview_interval_set(const struct shell *sh, size_t argc, char **argv)
{
	char *endptr;
	long val = strtol(argv[1], &endptr, 10);

	if (*endptr != '\0' || val < PREVIEW_INTERVAL_MIN_MS || val > PREVIEW_INTERVAL_MAX_MS) {
		shell_error(sh, "interval must be an integer between %d and %d ms",
			    PREVIEW_INTERVAL_MIN_MS, PREVIEW_INTERVAL_MAX_MS);
		return -EINVAL;
	}

	atomic_set(&g_preview_interval_ms, (atomic_val_t)val);
	shell_print(sh, "preview interval set to %ld ms (takes effect on the next frame)", val);
	return 0;
}

static int cmd_preview_interval_get(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "preview interval: %ld ms", (long)atomic_get(&g_preview_interval_ms));
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_preview_interval,
	SHELL_CMD(get, NULL, "Show the current preview interval", cmd_preview_interval_get),
	SHELL_CMD_ARG(set, NULL, "Set the preview interval in ms: preview interval set <ms>",
		      cmd_preview_interval_set, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_preview,
	SHELL_CMD(interval, &sub_preview_interval,
		  "Get/set how often a captured frame is pushed to the TFT", NULL),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(preview, &sub_preview, "Camera TFT preview controls", NULL);

static int configure_video_format(const struct device *video_dev, struct video_format *fmt)
{
	int ret;

	*fmt = (struct video_format) {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_SAMPLE_PIXEL_FORMAT,
		.width = VIDEO_SAMPLE_FRAME_WIDTH,
		.height = VIDEO_SAMPLE_FRAME_HEIGHT,
		.pitch = VIDEO_SAMPLE_FRAME_PITCH,
	};

	ret = video_set_format(video_dev, fmt);
	if (ret < 0) {
		LOG_ERR("video_set_format failed: %d", ret);
		return ret;
	}

	return 0;
}

int main(void)
{
	const struct device *video_dev = DEVICE_DT_GET(VIDEO_DEV_NODE);
	struct video_caps caps = { .type = VIDEO_BUF_TYPE_OUTPUT };
	struct video_format fmt;
	struct video_buffer *queued_buffers[VIDEO_SAMPLE_BUFFER_COUNT] = { 0 };
	struct video_buffer *captured = NULL;
	int ret;
	int final_ret = 0;
	size_t driver_owned_buffer_count = 0U;
	bool video_started = false;
	uint32_t frame_idx = 0U;

	LOG_INF("=== SR110 Camera TFT Preview (interval=%ld ms, 'preview interval set <ms>' to change) ===",
		(long)atomic_get(&g_preview_interval_ms));

	tft_preview_init();

	if (!device_is_ready(video_dev)) {
		LOG_ERR("Video device %s is not ready", video_dev->name);
		return -ENODEV;
	}

	ret = video_get_caps(video_dev, &caps);
	if (ret < 0) {
		LOG_ERR("video_get_caps failed: %d", ret);
		return ret;
	}

	if (ARRAY_SIZE(queued_buffers) < caps.min_vbuf_count) {
		LOG_ERR("Sample provides %u buffers but driver requires %u",
			(uint32_t)ARRAY_SIZE(queued_buffers), caps.min_vbuf_count);
		return -ENOMEM;
	}

	ret = configure_video_format(video_dev, &fmt);
	if (ret < 0) {
		return ret;
	}

	size_t align = MAX(caps.buf_align, 1U);

	for (size_t i = 0; i < ARRAY_SIZE(queued_buffers); i++) {
		queued_buffers[i] = video_buffer_aligned_alloc(fmt.size, align, K_NO_WAIT);
		if (queued_buffers[i] == NULL) {
			LOG_ERR("Buffer alloc failed. Increase CONFIG_VIDEO_BUFFER_POOL_HEAP_SIZE");
			final_ret = -ENOMEM;
			goto out_cleanup;
		}

		queued_buffers[i]->type = VIDEO_BUF_TYPE_OUTPUT;
		queued_buffers[i]->bytesused = 0U;

		ret = video_enqueue(video_dev, queued_buffers[i]);
		if (ret < 0) {
			LOG_ERR("video_enqueue failed: %d", ret);
			final_ret = ret;
			goto out_cleanup;
		}

		driver_owned_buffer_count++;
	}

	ret = video_stream_start(video_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		LOG_ERR("video_stream_start failed: %d", ret);
		final_ret = ret;
		goto out_cleanup;
	}
	video_started = true;

	/* Runs forever: this lab is a live preview, meant to be watched on
	 * the panel rather than read off a log, so there is no fixed frame
	 * count like lab_c2_continuous_stats. Serial output is kept to one
	 * short line per cycle on purpose - a busy console at a fast
	 * interval was making both the log and the panel hard to follow.
	 */
	while (1) {
		ret = video_dequeue(video_dev, &captured, VIDEO_SAMPLE_CAPTURE_TIMEOUT);
		if (ret < 0) {
			LOG_ERR("Capture timed out or failed (frame %u): %d",
				(uint32_t)(frame_idx + 1U), ret);
			final_ret = ret;
			goto out_stop_video;
		}
		driver_owned_buffer_count--;
		frame_idx++;

		tft_preview_show_frame(captured->buffer, VIDEO_SAMPLE_FRAME_WIDTH,
					VIDEO_SAMPLE_FRAME_HEIGHT);

		LOG_INF("Frame %u: capture -> display", frame_idx);

		captured->bytesused = 0U;
		ret = video_enqueue(video_dev, captured);
		if (ret < 0) {
			LOG_ERR("video_enqueue failed after frame %u: %d", frame_idx, ret);
			final_ret = ret;
			goto out_stop_video;
		}
		driver_owned_buffer_count++;

		k_sleep(K_MSEC(atomic_get(&g_preview_interval_ms)));
	}

out_stop_video:
	if (video_started) {
		ret = video_stream_stop(video_dev, VIDEO_BUF_TYPE_OUTPUT);
		if (ret < 0) {
			LOG_WRN("video_stream_stop failed: %d", ret);
			if (final_ret == 0) {
				final_ret = ret;
			}
		}
	}

out_cleanup:
	if (driver_owned_buffer_count > 0U) {
		ret = video_flush(video_dev, true);
		if (ret < 0) {
			LOG_WRN("video_flush(cancel=true) failed during buffer reclaim: %d", ret);
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(queued_buffers); i++) {
		if (queued_buffers[i] != NULL) {
			video_buffer_release(queued_buffers[i]);
			queued_buffers[i] = NULL;
		}
	}

	LOG_INF("Camera TFT preview sample finished (ret=%d)", final_ret);
	return final_ret;
}
