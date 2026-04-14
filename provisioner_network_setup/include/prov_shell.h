/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef PROV_SHELL_H__
#define PROV_SHELL_H__

#include <stddef.h>
#include <stdint.h>

/** Called from mesh unprovisioned beacon callback; updates fallback UUID state and auto policy. */
void prov_shell_feed_beacon(const uint8_t uuid[16]);

/** Re-evaluate whether this UUID became visible in the scan list. */
void prov_shell_note_uuid_update(const uint8_t uuid[16]);

/** Schedule a one-shot auto retry pass. */
void prov_shell_auto_retry_submit(void);

/** Snapshot count of visible UUIDs (for provision-by-index). */
size_t prov_shell_scan_count(void);

/** Copy visible UUID at index; returns 0 or -EINVAL / -ENOENT. */
int prov_shell_get_cached_uuid(size_t idx, uint8_t uuid[16]);

/** Returns true if auto-provisioning mode is enabled. */
bool prov_shell_auto_provision_enabled(void);

#endif /* PROV_SHELL_H__ */
