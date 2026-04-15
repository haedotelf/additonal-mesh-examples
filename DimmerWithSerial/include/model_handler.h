/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief Model handler
 */

#ifndef MODEL_HANDLER_H__
#define MODEL_HANDLER_H__

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/mesh.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct bt_mesh_comp *model_handler_init(void);
int model_handler_onoff_set(bool turn_on);
int model_handler_dim_step(bool dim_up, uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif /* MODEL_HANDLER_H__ */
