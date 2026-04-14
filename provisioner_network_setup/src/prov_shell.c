/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>

#include "prov_cache.h"
#include "mesh_net_cfg.h"
#include "prov_shell.h"
#include "provisioner_handler.h"
#include "scenario_network1.h"

#define PROV_AUTO_RETRY_MS CONFIG_MESH_PROVISIONER_AUTO_RETRY_MS

static bool scan_active;
static bool auto_provision_enabled;
static K_MUTEX_DEFINE(prov_shell_mutex);
static void auto_retry_work_handler(struct k_work *work);
static K_WORK_DEFINE(auto_retry_work, auto_retry_work_handler);

enum auto_try_result {
	AUTO_TRY_NONE,
	AUTO_TRY_REQUESTED,
	AUTO_TRY_BUSY,
	AUTO_TRY_ERROR,
};

static bool scan_is_active(void)
{
	bool active;

	k_mutex_lock(&prov_shell_mutex, K_FOREVER);
	active = scan_active;
	k_mutex_unlock(&prov_shell_mutex);

	return active;
}

static void maybe_print_visible_uuid(const uint8_t uuid[16])
{
	size_t idx;
	char hex[33];
	char name[PROV_CACHE_NAME_BUF_SIZE];

	if (!scan_is_active()) {
		return;
	}

	if (!prov_cache_maybe_mark_visible(uuid, &idx)) {
		return;
	}

	if (!prov_cache_lookup_name(uuid, name, sizeof(name))) {
		return;
	}

	bin2hex(uuid, 16, hex, sizeof(hex));
	printk("[%zu] : %s (%s)\n", idx, hex, name);
}

static enum auto_try_result auto_try_uuid(const uint8_t uuid[16])
{
	char name[PROV_CACHE_NAME_BUF_SIZE];
	const char *name_ptr = NULL;
	uint8_t recipe_id;
	int64_t now_ms = k_uptime_get();
	int err;

	if (!prov_cache_should_auto_attempt(uuid, now_ms, PROV_AUTO_RETRY_MS)) {
		return AUTO_TRY_NONE;
	}

	if (prov_cache_lookup_name(uuid, name, sizeof(name))) {
		name_ptr = name;
	}

	recipe_id = mesh_scenario_lookup_recipe_id(uuid, name_ptr, scenario_b1_corridor,
						      scenario_b1_corridor_len);
	if (recipe_id == RECIPE_NONE) {
		return AUTO_TRY_NONE;
	}

	err = provisioner_handler_request_provision_recipe(uuid, recipe_id);
	if (err == 0) {
		/* Rotating rows advance in configure_node() after recipe apply succeeds. */
		prov_cache_mark_attempt(uuid, now_ms);
		return AUTO_TRY_REQUESTED;
	}

	if (err == -EBUSY) {
		/* Apply cooldown so this UUID does not starve later candidates. */
		prov_cache_mark_attempt(uuid, now_ms);
		return AUTO_TRY_BUSY;
	}

	if (err != -ENODEV) {
		printk("prov auto: request failed (err %d)\n", err);
	}

	return AUTO_TRY_ERROR;
}

void prov_shell_note_uuid_update(const uint8_t uuid[16])
{
	maybe_print_visible_uuid(uuid);
}

void prov_shell_feed_beacon(const uint8_t uuid[16])
{
	(void)prov_cache_ensure_uuid(uuid);
	prov_shell_note_uuid_update(uuid);

	if (!prov_shell_auto_provision_enabled()) {
		return;
	}

	(void)auto_try_uuid(uuid);
}

/*
 * One-shot pass: walk cache slots in index order, skipping UUIDs that are not
 * ready (NONE) or whose provisioner slot is busy (BUSY). Stop only when a
 * request is submitted (REQUESTED) or all candidates are exhausted. Busy UUIDs
 * already have their cooldown stamped in auto_try_uuid(), so they will not be
 * visited again until PROV_AUTO_RETRY_MS elapses.
 */
static void auto_retry_work_handler(struct k_work *work)
{
	size_t cursor = 0U;
	uint8_t uuid[16];

	ARG_UNUSED(work);

	if (!prov_shell_auto_provision_enabled()) {
		return;
	}

	while (prov_cache_find_next_auto_candidate(&cursor, k_uptime_get(),
						       PROV_AUTO_RETRY_MS, uuid)) {
		enum auto_try_result result = auto_try_uuid(uuid);

		if (result == AUTO_TRY_REQUESTED) {
			return;
		}

		/* NONE, BUSY, or ERROR: continue to the next candidate. */
	}
}

void prov_shell_auto_retry_submit(void)
{
	if (!prov_shell_auto_provision_enabled()) {
		return;
	}

	(void)k_work_submit(&auto_retry_work);
}

size_t prov_shell_scan_count(void)
{
	return prov_cache_visible_count();
}

int prov_shell_get_cached_uuid(size_t idx, uint8_t uuid[16])
{
	return prov_cache_visible_uuid_by_index(idx, uuid);
}

bool prov_shell_auto_provision_enabled(void)
{
	bool enabled;

	k_mutex_lock(&prov_shell_mutex, K_FOREVER);
	enabled = auto_provision_enabled;
	k_mutex_unlock(&prov_shell_mutex);

	return enabled;
}

static int scan_set_state(const struct shell *sh, const char *arg)
{
	bool on;

	if (strcmp(arg, "1") == 0 || strcmp(arg, "on") == 0) {
		on = true;
	} else if (strcmp(arg, "0") == 0 || strcmp(arg, "off") == 0) {
		on = false;
	} else {
		shell_error(sh, "Use 1 or on to start scan, 0 or off to stop.");
		return -EINVAL;
	}

	k_mutex_lock(&prov_shell_mutex, K_FOREVER);
	scan_active = on;
	k_mutex_unlock(&prov_shell_mutex);

	if (on) {
		prov_cache_mark_all_ready_visible();
	}

	shell_print(sh, "Unprovisioned beacon scan: %s", on ? "on" : "off");
	return 0;
}

static void scan_print_list(const struct shell *sh)
{
	uint8_t uuid[16];
	char hex[33];
	char name[PROV_CACHE_NAME_BUF_SIZE];
	bool scan_on;
	bool auto_on;
	bool any = false;

	k_mutex_lock(&prov_shell_mutex, K_FOREVER);
	scan_on = scan_active;
	auto_on = auto_provision_enabled;
	k_mutex_unlock(&prov_shell_mutex);

	shell_print(sh, "Scanning: %s  Auto: %s",
		    scan_on ? "on" : "off",
		    auto_on ? "on" : "off");

	for (size_t i = 0; prov_cache_visible_uuid_by_index(i, uuid) == 0; i++) {
		if (!prov_cache_lookup_name(uuid, name, sizeof(name))) {
			continue;
		}

		any = true;
		bin2hex(uuid, 16, hex, sizeof(hex));
		shell_print(sh, "[%zu] : %s (%s)", i, hex, name);
	}

	if (!any) {
		shell_print(sh, "(no named unprovisioned devices in scan cache yet)");
	}
}

static int cmd_scan(const struct shell *sh, size_t argc, char **argv)
{
	if (argc >= 2) {
		int ret = scan_set_state(sh, argv[1]);

		if (ret == 0) {
			shell_print(sh, "");
			scan_print_list(sh);
		}
		return ret;
	}

	scan_print_list(sh);
	return 0;
}

static int parse_target(const char *arg, uint8_t uuid[16])
{
	size_t len = strlen(arg);
	size_t out_len;

	if (len == 32U) {
		out_len = hex2bin(arg, 32, uuid, 16);
		if (out_len != 16U) {
			return -EINVAL;
		}
		return 0;
	}

	char *end = NULL;
	long idx = strtol(arg, &end, 10);

	if ((end == arg) || (end != NULL && *end != '\0')) {
		return -EINVAL;
	}

	if (idx < 0) {
		return -EINVAL;
	}

	return prov_shell_get_cached_uuid((size_t)idx, uuid);
}

static int cmd_provision(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t uuid[16];
	int err;

	ARG_UNUSED(argc);

	err = parse_target(argv[1], uuid);
	if (err == -ENOENT) {
		shell_error(sh, "Index out of range (run `prov scan` to list cache).");
		return -ENOENT;
	}
	if (err != 0) {
		shell_error(sh, "Expected 32 hex chars (UUID) or decimal scan index.");
		return -EINVAL;
	}

	err = provisioner_handler_request_provision(uuid);
	if (err == -EBUSY) {
		shell_error(sh, "Provisioning already in progress.");
		return err;
	}
	if (err == -ENODEV) {
		shell_error(sh, "Mesh provisioner not ready.");
		return err;
	}

	shell_print(sh, "Provisioning requested.");
	return 0;
}

static int cmd_auto(const struct shell *sh, size_t argc, char **argv)
{
	bool on;

	ARG_UNUSED(argc);

	if (strcmp(argv[1], "1") == 0 || strcmp(argv[1], "on") == 0) {
		on = true;
	} else if (strcmp(argv[1], "0") == 0 || strcmp(argv[1], "off") == 0) {
		on = false;
	} else {
		shell_error(sh, "Use 1 or on to enable auto, 0 or off to disable.");
		return -EINVAL;
	}

	k_mutex_lock(&prov_shell_mutex, K_FOREVER);
	auto_provision_enabled = on;
	k_mutex_unlock(&prov_shell_mutex);

	if (on) {
		prov_shell_auto_retry_submit();
	}

	shell_print(sh, "Auto provisioning: %s", on ? "enabled" : "disabled");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_prov,
	SHELL_CMD_ARG(auto, NULL,
		      "Auto-provision each new beacon as it arrives: 1|on / 0|off",
		      cmd_auto, 2, 0),
	SHELL_CMD_ARG(scan, NULL,
		      "List named scan cache; 1|on / 0|off toggles display updates",
		      cmd_scan, 1, 1),
	SHELL_CMD_ARG(provision, NULL,
		      "Provision: 32-char hex UUID or decimal scan index",
		      cmd_provision, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(prov, &sub_prov, "Bluetooth Mesh provisioner", NULL);
