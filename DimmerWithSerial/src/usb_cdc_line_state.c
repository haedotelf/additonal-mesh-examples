/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * USB CDC ACM line state handler for boards that route the Zephyr shell to a
 * CDC ACM UART (nRF52840 DK and nRF52840 Dongle). On boards using a physical
 * UART (e.g. nRF54L15 DK), the compile-time guard below makes this file a
 * no-op.
 *
 * When a host opens the serial port (asserts DTR), this module signals DCD and
 * DSR back to the host so that terminal tools recognise the port as ready.
 *
 * SYS_INIT priority (52) must be higher than
 * CONFIG_SHELL_BACKEND_SERIAL_INIT_PRIORITY (51 in board conf files) so the
 * shell backend is already initialised when this handler registers itself.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/usbd_msg.h>

LOG_MODULE_REGISTER(usb_cdc_line_state, LOG_LEVEL_INF);

#if defined(CONFIG_USB_DEVICE_STACK_NEXT) && \
	DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_shell_uart), zephyr_cdc_acm_uart)

static const struct device *const shell_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
static const struct device *const usb_dev = DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0));

static void usb_cdc_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg)
{
	uint32_t dtr = 0U;

	ARG_UNUSED(ctx);

	if (msg->type != USBD_MSG_CDC_ACM_CONTROL_LINE_STATE || msg->dev != shell_uart) {
		return;
	}

	if (uart_line_ctrl_get(shell_uart, UART_LINE_CTRL_DTR, &dtr) || !dtr) {
		return;
	}

	/* Match the reference CDC ACM samples and tell the host the port is ready. */
	(void)uart_line_ctrl_set(shell_uart, UART_LINE_CTRL_DCD, 1);
	(void)uart_line_ctrl_set(shell_uart, UART_LINE_CTRL_DSR, 1);
}

static int usb_cdc_line_state_init(void)
{
	int err;

	if (!device_is_ready(shell_uart) || !device_is_ready(usb_dev)) {
		return 0;
	}

	STRUCT_SECTION_FOREACH(usbd_context, ctx) {
		if (ctx->dev != usb_dev) {
			continue;
		}

		err = usbd_msg_register_cb(ctx, usb_cdc_msg_cb);
		if (err == -EALREADY) {
			return 0;
		}

		return err;
	}

	return 0;
}

SYS_INIT(usb_cdc_line_state_init, POST_KERNEL, 52);

#endif
