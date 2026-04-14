/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "prov_cache.h"

#define PROV_CACHE_MAX CONFIG_MESH_PROVISIONER_SCAN_UUID_CACHE_MAX
#define NAME_CAP (PROV_CACHE_NAME_BUF_SIZE - 1U)

struct prov_cache_entry {
	bt_addr_le_t addr;
	uint8_t uuid[16];
	char name[PROV_CACHE_NAME_BUF_SIZE];
	bool in_use;
	bool have_addr;
	bool have_uuid;
	bool have_name;
	bool scan_visible;
	int64_t last_attempt_ms;
};

static struct prov_cache_entry cache[PROV_CACHE_MAX];
static size_t evict_cursor;
static K_MUTEX_DEFINE(cache_mutex);

static void entry_clear(struct prov_cache_entry *entry)
{
	memset(entry, 0, sizeof(*entry));
	entry->last_attempt_ms = INT64_MIN;
}

static struct prov_cache_entry *find_by_addr_locked(const bt_addr_le_t *addr)
{
	for (size_t i = 0; i < ARRAY_SIZE(cache); i++) {
		if (!cache[i].in_use || !cache[i].have_addr) {
			continue;
		}

		if (bt_addr_le_cmp(&cache[i].addr, addr) == 0) {
			return &cache[i];
		}
	}

	return NULL;
}

static struct prov_cache_entry *find_by_uuid_locked(const uint8_t uuid[16])
{
	for (size_t i = 0; i < ARRAY_SIZE(cache); i++) {
		if (!cache[i].in_use || !cache[i].have_uuid) {
			continue;
		}

		if (memcmp(cache[i].uuid, uuid, sizeof(cache[i].uuid)) == 0) {
			return &cache[i];
		}
	}

	return NULL;
}

static bool evict_match(const struct prov_cache_entry *entry, int pass)
{
	switch (pass) {
	case 0:
		return !entry->scan_visible && !entry->have_name;
	case 1:
		return !entry->scan_visible;
	case 2:
		return true;
	default:
		return false;
	}
}

static struct prov_cache_entry *alloc_entry_locked(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(cache); i++) {
		if (cache[i].in_use) {
			continue;
		}

		entry_clear(&cache[i]);
		cache[i].in_use = true;
		return &cache[i];
	}

	for (int pass = 0; pass < 3; pass++) {
		for (size_t offset = 0; offset < ARRAY_SIZE(cache); offset++) {
			size_t idx = (evict_cursor + offset) % ARRAY_SIZE(cache);

			if (!evict_match(&cache[idx], pass)) {
				continue;
			}

			evict_cursor = (idx + 1U) % ARRAY_SIZE(cache);
			entry_clear(&cache[idx]);
			cache[idx].in_use = true;
			return &cache[idx];
		}
	}

	return NULL;
}

static void name_set_locked(struct prov_cache_entry *entry, const uint8_t *data, size_t len)
{
	size_t n = MIN(len, (size_t)NAME_CAP);

	memcpy(entry->name, data, n);
	entry->name[n] = '\0';
	entry->have_name = true;
}

static bool retry_ready_locked(const struct prov_cache_entry *entry, int64_t now_ms,
			       int32_t retry_ms)
{
	if (!entry->in_use || !entry->have_uuid) {
		return false;
	}

	if (entry->last_attempt_ms != INT64_MIN &&
	    now_ms - entry->last_attempt_ms < (int64_t)retry_ms) {
		return false;
	}

	return true;
}

static size_t visible_index_locked(const struct prov_cache_entry *needle)
{
	size_t idx = 0;

	for (size_t i = 0; i < ARRAY_SIZE(cache); i++) {
		if (!cache[i].in_use || !cache[i].scan_visible) {
			continue;
		}

		if (&cache[i] == needle) {
			return idx;
		}

		idx++;
	}

	return idx;
}

int prov_cache_adv_uuid_seen(const bt_addr_le_t *addr, const uint8_t uuid[16])
{
	struct prov_cache_entry *by_addr;
	struct prov_cache_entry *by_uuid;
	struct prov_cache_entry *entry;

	k_mutex_lock(&cache_mutex, K_FOREVER);

	by_addr = find_by_addr_locked(addr);
	by_uuid = find_by_uuid_locked(uuid);

	if (by_addr != NULL && by_uuid != NULL && by_addr != by_uuid) {
		if (!by_uuid->have_name && by_addr->have_name) {
			memcpy(by_uuid->name, by_addr->name, sizeof(by_uuid->name));
			by_uuid->have_name = true;
		}
		if (by_addr->scan_visible) {
			by_uuid->scan_visible = true;
		}
		entry_clear(by_addr);
		by_addr->in_use = false;
		entry = by_uuid;
	} else if (by_uuid != NULL) {
		entry = by_uuid;
	} else if (by_addr != NULL) {
		entry = by_addr;
		if (entry->have_uuid &&
		    memcmp(entry->uuid, uuid, sizeof(entry->uuid)) != 0) {
			entry_clear(entry);
			entry->in_use = true;
		}
	} else {
		entry = alloc_entry_locked();
		if (entry == NULL) {
			k_mutex_unlock(&cache_mutex);
			return -ENOMEM;
		}
	}

	bt_addr_le_copy(&entry->addr, addr);
	memcpy(entry->uuid, uuid, sizeof(entry->uuid));
	entry->have_addr = true;
	entry->have_uuid = true;

	k_mutex_unlock(&cache_mutex);

	return 0;
}

bool prov_cache_scan_rsp_name_seen(const bt_addr_le_t *addr, const uint8_t *name, size_t len,
				   uint8_t uuid_out[16])
{
	struct prov_cache_entry *entry;
	bool have_uuid = false;

	k_mutex_lock(&cache_mutex, K_FOREVER);

	entry = find_by_addr_locked(addr);
	if (entry != NULL) {
		name_set_locked(entry, name, len);
		if (entry->have_uuid) {
			memcpy(uuid_out, entry->uuid, sizeof(entry->uuid));
			have_uuid = true;
		}
	}

	k_mutex_unlock(&cache_mutex);

	return have_uuid;
}

int prov_cache_ensure_uuid(const uint8_t uuid[16])
{
	struct prov_cache_entry *entry;

	k_mutex_lock(&cache_mutex, K_FOREVER);

	entry = find_by_uuid_locked(uuid);
	if (entry == NULL) {
		entry = alloc_entry_locked();
		if (entry == NULL) {
			k_mutex_unlock(&cache_mutex);
			return -ENOMEM;
		}

		memcpy(entry->uuid, uuid, sizeof(entry->uuid));
		entry->have_uuid = true;
	}

	k_mutex_unlock(&cache_mutex);

	return 0;
}

bool prov_cache_lookup_name(const uint8_t uuid[16], char *name, size_t name_len)
{
	struct prov_cache_entry *entry;
	bool found = false;

	if (name_len == 0U) {
		return false;
	}

	k_mutex_lock(&cache_mutex, K_FOREVER);

	entry = find_by_uuid_locked(uuid);
	if (entry != NULL && entry->have_name) {
		strncpy(name, entry->name, name_len - 1U);
		name[name_len - 1U] = '\0';
		found = true;
	}

	k_mutex_unlock(&cache_mutex);

	return found;
}

bool prov_cache_maybe_mark_visible(const uint8_t uuid[16], size_t *visible_idx)
{
	struct prov_cache_entry *entry;
	bool marked = false;

	k_mutex_lock(&cache_mutex, K_FOREVER);

	entry = find_by_uuid_locked(uuid);
	if (entry != NULL && entry->have_name && !entry->scan_visible) {
		entry->scan_visible = true;
		if (visible_idx != NULL) {
			*visible_idx = visible_index_locked(entry);
		}
		marked = true;
	}

	k_mutex_unlock(&cache_mutex);

	return marked;
}

void prov_cache_mark_all_ready_visible(void)
{
	k_mutex_lock(&cache_mutex, K_FOREVER);

	for (size_t i = 0; i < ARRAY_SIZE(cache); i++) {
		if (!cache[i].in_use || !cache[i].have_uuid || !cache[i].have_name) {
			continue;
		}

		cache[i].scan_visible = true;
	}

	k_mutex_unlock(&cache_mutex);
}

size_t prov_cache_visible_count(void)
{
	size_t count = 0;

	k_mutex_lock(&cache_mutex, K_FOREVER);

	for (size_t i = 0; i < ARRAY_SIZE(cache); i++) {
		if (cache[i].in_use && cache[i].scan_visible) {
			count++;
		}
	}

	k_mutex_unlock(&cache_mutex);

	return count;
}

int prov_cache_visible_uuid_by_index(size_t idx, uint8_t uuid[16])
{
	size_t visible_idx = 0;
	int err = -ENOENT;

	k_mutex_lock(&cache_mutex, K_FOREVER);

	for (size_t i = 0; i < ARRAY_SIZE(cache); i++) {
		if (!cache[i].in_use || !cache[i].scan_visible) {
			continue;
		}

		if (visible_idx == idx) {
			memcpy(uuid, cache[i].uuid, sizeof(cache[i].uuid));
			err = 0;
			break;
		}

		visible_idx++;
	}

	k_mutex_unlock(&cache_mutex);

	return err;
}

bool prov_cache_should_auto_attempt(const uint8_t uuid[16], int64_t now_ms, int32_t retry_ms)
{
	struct prov_cache_entry *entry;
	bool should_attempt = false;

	k_mutex_lock(&cache_mutex, K_FOREVER);

	entry = find_by_uuid_locked(uuid);
	if (entry != NULL) {
		should_attempt = retry_ready_locked(entry, now_ms, retry_ms);
	}

	k_mutex_unlock(&cache_mutex);

	return should_attempt;
}

bool prov_cache_find_next_auto_candidate(size_t *cursor, int64_t now_ms, int32_t retry_ms,
					 uint8_t uuid[16])
{
	bool found = false;

	k_mutex_lock(&cache_mutex, K_FOREVER);

	for (size_t i = *cursor; i < ARRAY_SIZE(cache); i++) {
		if (!retry_ready_locked(&cache[i], now_ms, retry_ms)) {
			continue;
		}

		memcpy(uuid, cache[i].uuid, sizeof(cache[i].uuid));
		*cursor = i + 1U;
		found = true;
		break;
	}

	k_mutex_unlock(&cache_mutex);

	return found;
}

void prov_cache_mark_attempt(const uint8_t uuid[16], int64_t now_ms)
{
	struct prov_cache_entry *entry;

	k_mutex_lock(&cache_mutex, K_FOREVER);

	entry = find_by_uuid_locked(uuid);
	if (entry != NULL) {
		entry->last_attempt_ms = now_ms;
	}

	k_mutex_unlock(&cache_mutex);
}

void prov_cache_on_configuration_success(const uint8_t uuid[16])
{
	struct prov_cache_entry *entry;

	k_mutex_lock(&cache_mutex, K_FOREVER);

	entry = find_by_uuid_locked(uuid);
	if (entry != NULL) {
		entry_clear(entry);
		entry->in_use = false;
	}

	k_mutex_unlock(&cache_mutex);
}
