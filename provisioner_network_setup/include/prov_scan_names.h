/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef PROV_SCAN_NAMES_H__
#define PROV_SCAN_NAMES_H__

#include <stdbool.h>

/** Register LE scan listener; call before mesh starts scanning. */
void prov_scan_names_init(void);

/**
 * Switch mesh LE scan to active so SCAN_RSP (GAP name on PB-GATT devices) is received.
 * Uses Zephyr mesh stack routine (same scan as mesh, not a second controller scan).
 */
int prov_scan_names_set_mesh_active_scan(bool active);

#endif /* PROV_SCAN_NAMES_H__ */
