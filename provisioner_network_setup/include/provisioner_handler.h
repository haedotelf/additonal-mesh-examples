/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Bluetooth Mesh provisioner (CDB) logic
 */

#ifndef PROVISIONER_HANDLER_H__
#define PROVISIONER_HANDLER_H__

#include <stdint.h>
#include <zephyr/bluetooth/mesh.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct bt_mesh_prov *provisioner_handler_prov(void);
const struct bt_mesh_comp *provisioner_handler_init(void);
int provisioner_handler_bluetooth_ready(void);

/** Queue provisioning for the given device UUID (PB-ADV). Returns 0, -EBUSY, or -ENODEV. */
int provisioner_handler_request_provision(const uint8_t uuid[16]);

/**
 * Queue provisioning for the given device UUID and carry the selected recipe into
 * post-provision configuration. Use RECIPE_NONE when no preselection exists.
 */
int provisioner_handler_request_provision_recipe(const uint8_t uuid[16], uint8_t recipe_id);

#ifdef __cplusplus
}
#endif

#endif /* PROVISIONER_HANDLER_H__ */
