/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "scenario_network1.h"

#include <zephyr/sys/util.h>

/*
 * Scenario: Building 1, corridor lighting (+ EnOcean broadcast). Row order does not affect
 * longest-prefix resolution, except when two rows have the same prefix length and both match
 * (first row wins).
 */
struct scenario_row scenario_b1_corridor[] = {

	NODE_NAME_ROTATE("Mesh Light Switch", RECIPE_LIGHT_SW_1, RECIPE_LIGHT_SW_2),
	NODE_NAME_ROTATE("Mesh Light Fixture", RECIPE_LIGHT_CTRL_1, RECIPE_LIGHT_CTRL_2),
	NODE_NAME("Mesh Silvair EnOcean", RECIPE_ENOCEAN_BCAST),

	// Some examples of how to add specific devices:
	// NODE_FULL("A002000000000000DEADBEEF00000000", RECIPE_LIGHT_CTRL_1),
	// NODE_FULL("A002000000000000DEADBEEF00000001", RECIPE_LIGHT_CTRL_2),
	// NODE_FULL("B002000000000000CAFEBABE00000001", RECIPE_LIGHT_SW_2),
	// NODE_FULL("C001000000000000CAFEBABE00000001", RECIPE_DIMMER_1),

	// NODE_PREFIX("A001", RECIPE_LIGHT_CTRL_1),
	// NODE_PREFIX("A002", RECIPE_LIGHT_CTRL_2),
	// NODE_PREFIX("B001", RECIPE_LIGHT_SW_1),
	// NODE_PREFIX("B002", RECIPE_LIGHT_SW_2),
	// NODE_PREFIX("C001", RECIPE_DIMMER_1),
	// NODE_PREFIX("C002", RECIPE_DIMMER_2),
	// NODE_PREFIX("D001", RECIPE_ENOCEAN_BCAST),
};

const size_t scenario_b1_corridor_len = ARRAY_SIZE(scenario_b1_corridor);
