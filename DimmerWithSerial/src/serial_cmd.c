/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/shell/shell.h>

#include "model_handler.h"

#define DEFAULT_DIM_STEP_PERCENT 10U

static int command_status_report(const struct shell *sh, int err)
{
	if (!err) {
		return 0;
	}

	if (err == -EAGAIN) {
		shell_error(sh, "Node is not provisioned");
	} else if (err == -EINVAL) {
		shell_error(sh, "Invalid input");
	} else {
		shell_error(sh, "Command failed: %d", err);
	}

	return err;
}

static int parse_percent_arg(const struct shell *sh, const char *arg, uint8_t *percent)
{
	char *end;
	char percent_buf[5];
	unsigned long value;

	if (strlen(arg) >= sizeof(percent_buf)) {
		shell_error(sh, "Percentage must be in range 1-100");
		return -EINVAL;
	}

	strcpy(percent_buf, arg);
	end = strchr(percent_buf, '%');
	if (end != NULL) {
		if (*(end + 1) != '\0') {
			shell_error(sh, "Unexpected characters after %%");
			return -EINVAL;
		}
		*end = '\0';
	}

	value = strtoul(percent_buf, &end, 10);
	if ((*percent_buf == '\0') || (*end != '\0') || (value == 0U) || (value > 100U)) {
		shell_error(sh, "Percentage must be in range 1-100");
		return -EINVAL;
	}

	*percent = (uint8_t)value;
	return 0;
}

static int cmd_on(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return command_status_report(sh, model_handler_onoff_set(true));
}

static int cmd_off(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return command_status_report(sh, model_handler_onoff_set(false));
}

static int cmd_dim_common(const struct shell *sh, size_t argc, char **argv, bool dim_up)
{
	uint8_t percent = DEFAULT_DIM_STEP_PERCENT;
	int err;

	if (argc >= 2U) {
		err = parse_percent_arg(sh, argv[1], &percent);
		if (err) {
			return err;
		}
	}

	err = model_handler_dim_step(dim_up, percent);
	if (!err) {
		shell_print(sh, "Dim %s by %u%%", dim_up ? "up" : "down", percent);
	}

	return command_status_report(sh, err);
}

static int cmd_dim_up(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_dim_common(sh, argc, argv, true);
}

static int cmd_dim_down(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_dim_common(sh, argc, argv, false);
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	dim_cmds,
	SHELL_CMD_ARG(up, NULL, "[percent|percent%]", cmd_dim_up, 1, 1),
	SHELL_CMD_ARG(down, NULL, "[percent|percent%]", cmd_dim_down, 1, 1),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(on, NULL, "Turn the bound light on.", cmd_on);
SHELL_CMD_REGISTER(off, NULL, "Turn the bound light off.", cmd_off);
SHELL_CMD_REGISTER(dim, &dim_cmds, "Dim the bound light up or down.", NULL);
