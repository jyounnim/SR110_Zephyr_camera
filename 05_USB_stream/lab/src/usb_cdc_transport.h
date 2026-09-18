/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reused verbatim from the SDK's mipi_capture_to_enc sample
 * (samples/drivers/video/mipi_capture_to_enc/src/usb_cdc_transport.h).
 */

#ifndef LAB_C5_USB_CDC_TRANSPORT_H_
#define LAB_C5_USB_CDC_TRANSPORT_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

struct usbd_context;

struct cdc_transport_ctx {
	struct usbd_context *usbd_ctx;
	const struct device *cdc_dev;
	struct k_sem tx_done_sem;
	const uint8_t *tx_ptr;
	size_t tx_remaining;
};

int cdc_transport_init(struct cdc_transport_ctx *ctx);
int cdc_transport_wait_dtr(struct cdc_transport_ctx *ctx, k_timeout_t timeout);
int cdc_transport_send_jpeg(struct cdc_transport_ctx *ctx, uint16_t image_id,
			    const uint8_t *jpeg, size_t jpeg_len);

#endif /* LAB_C5_USB_CDC_TRANSPORT_H_ */
