/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef MESH_RECIPES_H_
#define MESH_RECIPES_H_

#include <zephyr/bluetooth/mesh.h>

#include "mesh_config_types.h"

/* ------------------------------------------------------------------ */
/* Network constants - adjust per deployment                           */
/* ------------------------------------------------------------------ */

#define NET_PRIMARY 0x000U /* primary network key index         */
#define APPKEY_IDX 0x000U /* app key index for all lighting    */

/* Group addresses. */
#define GRP_1 0xC001U
#define GRP_2 0xC002U

#define PUB_TTL 5
#define PUB_PERIOD_OFF 0 /* event-driven, no periodic pub */
#define PUB_XMIT_1x100 BT_MESH_PUB_TRANSMIT(1, 100) /* 1 retx, 100 ms */

/* ------------------------------------------------------------------ */
/* Recipe IDs                                                          */
/* ------------------------------------------------------------------ */

enum mesh_recipe_id {
	RECIPE_NONE = 0x00, /* reserved / unprovisioned  */
	RECIPE_LIGHT_CTRL_1 = 0x01,
	RECIPE_LIGHT_CTRL_2 = 0x02,
	RECIPE_DIMMER_1 = 0x03,
	RECIPE_DIMMER_2 = 0x04,
	RECIPE_LIGHT_SW_1 = 0x05,
	RECIPE_LIGHT_SW_2 = 0x06,
	RECIPE_ENOCEAN_BCAST = 0x07,
	RECIPE_COUNT,
};

extern const struct mesh_recipe mesh_recipes[RECIPE_COUNT];

#endif /* MESH_RECIPES_H_ */
