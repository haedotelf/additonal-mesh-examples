/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef PROV_CACHE_H__
#define PROV_CACHE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/addr.h>

/* Enough for typical GAP names in PB-GATT scan response without BT_DEVICE_NAME_MAX. */
#define PROV_CACHE_NAME_BUF_SIZE 33

int prov_cache_adv_uuid_seen(const bt_addr_le_t *addr, const uint8_t uuid[16]);
bool prov_cache_scan_rsp_name_seen(const bt_addr_le_t *addr, const uint8_t *name, size_t len,
				   uint8_t uuid_out[16]);
int prov_cache_ensure_uuid(const uint8_t uuid[16]);
bool prov_cache_lookup_name(const uint8_t uuid[16], char *name, size_t name_len);
bool prov_cache_maybe_mark_visible(const uint8_t uuid[16], size_t *visible_idx);
void prov_cache_mark_all_ready_visible(void);
size_t prov_cache_visible_count(void);
int prov_cache_visible_uuid_by_index(size_t idx, uint8_t uuid[16]);
bool prov_cache_should_auto_attempt(const uint8_t uuid[16], int64_t now_ms, int32_t retry_ms);
bool prov_cache_find_next_auto_candidate(size_t *cursor, int64_t now_ms, int32_t retry_ms,
					 uint8_t uuid[16]);
void prov_cache_mark_attempt(const uint8_t uuid[16], int64_t now_ms);
void prov_cache_on_configuration_success(const uint8_t uuid[16]);

#endif /* PROV_CACHE_H__ */
