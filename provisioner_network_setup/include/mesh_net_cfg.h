/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef MESH_NET_CFG_H_
#define MESH_NET_CFG_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/mesh/cdb.h>

struct scenario_row;

int mesh_config_apply(struct bt_mesh_cdb_node *node, struct scenario_row *rows,
		      size_t n_rows, const char *name);

int mesh_config_apply_recipe(struct bt_mesh_cdb_node *node, uint8_t recipe_id);

const char *mesh_recipe_name(uint8_t recipe_id);

uint8_t mesh_scenario_lookup_recipe_id(const uint8_t uuid[16], const char *name,
				       struct scenario_row *rows, size_t n_rows);

/**
 * Advance rotating recipe state after the given recipe_id was successfully
 * applied to a node (no-op for non-rotating rows; no-op if peek no longer
 * matches recipe_id).
 */
void mesh_scenario_consume_recipe_id(const uint8_t uuid[16], const char *name,
				     struct scenario_row *rows, size_t n_rows,
				     uint8_t recipe_id);

#endif /* MESH_NET_CFG_H_ */
