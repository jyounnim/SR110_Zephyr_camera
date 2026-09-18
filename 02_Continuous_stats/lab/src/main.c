/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SR110 AI curriculum Lab C2 - Continuous Capture + Brightness Statistics.
 *
 * Builds on lab_c1_basic_capture's video_api capture loop (same devicetree
 * node, same WQVGA RAW8 format), but instead of just logging bytesused per
 * frame, computes a simple mean/min/max brightness over each frame's raw
 * bytes and keeps a running summary across the whole capture.
 *
 * The FHD/SHM branching from Lab C1 is intentionally dropped here - this
 * lab is about the capture loop and the statistics, not about resolution,
 * so the code stays WQVGA-only and simpler to read.
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/video/video.h>

LOG_MODULE_REGISTER(video_stats_app, CONFIG_LOG_DEFAULT_LEVEL);

#define VIDEO_SAMPLE_FRAME_WIDTH      ((uint32_t)CONFIG_VIDEO_SAMPLE_FRAME_WIDTH)
#define VIDEO_SAMPLE_FRAME_HEIGHT     ((uint32_t)CONFIG_VIDEO_SAMPLE_FRAME_HEIGHT)
#define VIDEO_SAMPLE_FRAME_PITCH      ((uint32_t)CONFIG_VIDEO_SAMPLE_FRAME_PITCH)
#define VIDEO_SAMPLE_BUFFER_COUNT     ((uint32_t)CONFIG_VIDEO_SAMPLE_BUFFER_COUNT)
#define VIDEO_SAMPLE_CAPTURE_TIMEOUT  K_MSEC(CONFIG_VIDEO_SAMPLE_CAPTURE_TIMEOUT_MS)
#define VIDEO_SAMPLE_CAPTURE_COUNT    ((uint32_t)CONFIG_VIDEO_SAMPLE_CAPTURE_COUNT)

#define VIDEO_SAMPLE_PIXEL_FORMAT     VIDEO_PIX_FMT_SRGGB8

#define VIDEO_DEV_NODE DT_NODELABEL(video_syna0)

#if !DT_NODE_EXISTS(VIDEO_DEV_NODE)
#error "Devicetree node 'video_syna0' is required for this sample"
#endif

/*
 * Brightness stats over a raw RAW8 Bayer buffer.
 *
 * This treats every raw byte as if it were a brightness sample. That is not
 * strictly correct (each byte is really only the R, G, or B component of one
 * Bayer cell, not a true per-pixel luma), but it is a good enough proxy for
 * "is the scene bright or dark, and how much does it vary" without doing a
 * full debayer + color-space conversion on the M55 CPU - which would be a
 * much heavier lab on its own.
 *
 * `sum` is kept alongside `mean_x100` (mean * 100, to avoid float printf)
 * so callers can accumulate an exact running total across many frames
 * instead of re-deriving it from the rounded mean each time.
 */
struct brightness_stats {
        uint64_t sum;
        uint32_t mean_x100;
        uint8_t min_val;
        uint8_t max_val;
};

static struct brightness_stats compute_brightness_stats(const uint8_t *buf, size_t len)
{
        struct brightness_stats stats = {
                .sum = 0U,
                .mean_x100 = 0U,
                .min_val = 0xFFU,
                .max_val = 0x00U,
        };

        for (size_t i = 0U; i < len; i++) {
                uint8_t value = buf[i];

                stats.sum += value;
                if (value < stats.min_val) {
                        stats.min_val = value;
                }
                if (value > stats.max_val) {
                        stats.max_val = value;
                }
        }

        if (len > 0U) {
                stats.mean_x100 = (uint32_t)((stats.sum * 100U) / len);
        }

        return stats;
}

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

        LOG_INF("Video format configured: %ux%u pitch=%u size=%u",
                fmt->width, fmt->height, fmt->pitch, fmt->size);

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

        /* Running totals across the whole capture, in addition to the
         * per-frame stats printed inside the loop below.
         */
        uint64_t overall_sum = 0U;
        uint64_t overall_pixels = 0U;
        uint8_t overall_min = 0xFFU;
        uint8_t overall_max = 0x00U;

        LOG_INF("=== SR110 Camera Continuous Capture + Brightness Stats ===");

        if (!device_is_ready(video_dev)) {
                LOG_ERR("Video device %s is not ready", video_dev->name);
                return -ENODEV;
        }

        ret = video_get_caps(video_dev, &caps);
        if (ret < 0) {
                LOG_ERR("video_get_caps failed: %d", ret);
                return ret;
        }

        LOG_INF("Video caps: min_vbuf_count=%u align=%u", caps.min_vbuf_count,
                (uint32_t)caps.buf_align);

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
        LOG_INF("Video stream started, capturing %u frames...",
                (uint32_t)VIDEO_SAMPLE_CAPTURE_COUNT);

        for (uint32_t frame_idx = 0U; frame_idx < VIDEO_SAMPLE_CAPTURE_COUNT; frame_idx++) {
                ret = video_dequeue(video_dev, &captured, VIDEO_SAMPLE_CAPTURE_TIMEOUT);
                if (ret < 0) {
                        LOG_ERR("Capture timed out or failed (frame %u): %d",
                                (uint32_t)(frame_idx + 1U), ret);
                        final_ret = ret;
                        goto out_stop_video;
                }
                driver_owned_buffer_count--;

                struct brightness_stats stats =
                        compute_brightness_stats(captured->buffer, captured->bytesused);

                LOG_INF("Frame %4u/%u: mean=%u.%02u max=%3u min=%3u (bytesused=%u)",
                        (uint32_t)(frame_idx + 1U), (uint32_t)VIDEO_SAMPLE_CAPTURE_COUNT,
                        stats.mean_x100 / 100U, stats.mean_x100 % 100U,
                        stats.max_val, stats.min_val, captured->bytesused);

                overall_sum += stats.sum;
                overall_pixels += captured->bytesused;
                if (stats.min_val < overall_min) {
                        overall_min = stats.min_val;
                }
                if (stats.max_val > overall_max) {
                        overall_max = stats.max_val;
                }

                captured->bytesused = 0U;
                ret = video_enqueue(video_dev, captured);
                if (ret < 0) {
                        LOG_ERR("video_enqueue failed after frame %u: %d",
                                (uint32_t)(frame_idx + 1U), ret);
                        final_ret = ret;
                        goto out_stop_video;
                }
                driver_owned_buffer_count++;
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

        if (overall_pixels > 0U) {
                uint32_t overall_mean_x100 = (uint32_t)((overall_sum * 100U) / overall_pixels);

                LOG_INF("=== Summary over %u frames: mean=%u.%02u max=%u min=%u ===",
                        (uint32_t)VIDEO_SAMPLE_CAPTURE_COUNT,
                        overall_mean_x100 / 100U, overall_mean_x100 % 100U,
                        overall_max, overall_min);
        }

        LOG_INF("Camera stats sample finished");
        return final_ret;
}
