/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef MESH_CONFIG_TYPES_H_
#define MESH_CONFIG_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/*
 * Maximum elements per node this configurator supports (stack buffers, loops).
 * Override from the application or via -DMESH_CONFIG_MAX_ELEM_PER_NODE=N.
 */
#ifndef MESH_CONFIG_MAX_ELEM_PER_NODE
#define MESH_CONFIG_MAX_ELEM_PER_NODE 5
#endif

/* ------------------------------------------------------------------ */
/* Enums                                                              */
/* ------------------------------------------------------------------ */

enum mesh_setup_op {
	MESH_OP_NETKEY_ADD, /* bt_mesh_cfg_cli_net_key_add           */
	MESH_OP_APPKEY_ADD, /* bt_mesh_cfg_cli_app_key_add           */
	MESH_OP_BIND_APP,   /* bt_mesh_cfg_cli_mod_app_bind[_vnd]    */
	MESH_OP_SUB_ADD,    /* bt_mesh_cfg_cli_mod_sub_add[_vnd]     */
	MESH_OP_PUB_SET,    /* bt_mesh_cfg_cli_mod_pub_set[_vnd]     */
};

/*
 * MESH_ELEM_ADDR_OFF: target = node->addr + elem_param.
 *   elem_param = 0  primary element.
 *   elem_param = N  Nth element (consecutive unicast).
 *
 * MESH_ELEM_ALL_WITH_MOD: expand to every element whose composition
 *   contains 'model'.  Requires composition data fetched beforehand.
 */
enum mesh_elem_sel {
	MESH_ELEM_ADDR_OFF,
	MESH_ELEM_ALL_WITH_MOD,
};

/* ------------------------------------------------------------------ */
/* Model reference                                                    */
/* ------------------------------------------------------------------ */

/* Sentinel matching the shell's CID_NVAL - marks a SIG model. */
#define MESH_CID_NVAL 0xffff

struct mesh_model_ref {
	uint16_t id;
	uint16_t cid; /* MESH_CID_NVAL for SIG models */
};

#define SIG_MODEL(_id) ((struct mesh_model_ref){ .id = (_id), .cid = MESH_CID_NVAL })

#define VND_MODEL(_id, _cid) ((struct mesh_model_ref){ .id = (_id), .cid = (_cid) })

/* ------------------------------------------------------------------ */
/* Setup step                                                          */
/* ------------------------------------------------------------------ */

struct mesh_setup_step {
	enum mesh_setup_op op;
	enum mesh_elem_sel elem_sel;
	uint16_t elem_param; /* offset (ADDR_OFF only)    */
	struct mesh_model_ref model; /* unused for key ops        */
	uint16_t app_idx; /* BIND and PUB only         */
	union {
		struct {
			uint16_t net_key_idx;
		} netkey;

		struct {
			uint16_t net_key_idx;
			uint16_t app_key_idx;
		} appkey;

		struct {
			uint16_t sub_addr;
		} sub;

		struct {
			uint16_t pub_addr;
			bool cred_flag;
			uint8_t ttl;
			uint8_t period;
			uint8_t transmit;
		} pub;
	} u;
};

/* ------------------------------------------------------------------ */
/* Step construction macros                                            */
/* ------------------------------------------------------------------ */

#define NETKEY_ADD(_net_key_idx)					\
	{								\
		.op = MESH_OP_NETKEY_ADD,				\
		.elem_sel = MESH_ELEM_ADDR_OFF,			\
		.elem_param = 0,					\
		.u.netkey = { .net_key_idx = (_net_key_idx) },		\
	}

#define APPKEY_ADD(_net_key_idx, _app_key_idx)				\
	{								\
		.op = MESH_OP_APPKEY_ADD,				\
		.elem_sel = MESH_ELEM_ADDR_OFF,			\
		.elem_param = 0,					\
		.u.appkey = {						\
			.net_key_idx = (_net_key_idx),			\
			.app_key_idx = (_app_key_idx),			\
		},							\
	}

#define BIND(_elem_off, _model, _app_idx)				\
	{								\
		.op = MESH_OP_BIND_APP,				\
		.elem_sel = MESH_ELEM_ADDR_OFF,			\
		.elem_param = (_elem_off),				\
		.model = (_model),					\
		.app_idx = (_app_idx),				\
	}

#define BIND_ALL(_model, _app_idx)					\
	{								\
		.op = MESH_OP_BIND_APP,				\
		.elem_sel = MESH_ELEM_ALL_WITH_MOD,			\
		.elem_param = 0,					\
		.model = (_model),					\
		.app_idx = (_app_idx),				\
	}

#define SUB(_elem_off, _model, _sub_addr)				\
	{								\
		.op = MESH_OP_SUB_ADD,				\
		.elem_sel = MESH_ELEM_ADDR_OFF,			\
		.elem_param = (_elem_off),				\
		.model = (_model),					\
		.u.sub = { .sub_addr = (_sub_addr) },			\
	}

#define SUB_ALL(_model, _sub_addr)					\
	{								\
		.op = MESH_OP_SUB_ADD,				\
		.elem_sel = MESH_ELEM_ALL_WITH_MOD,			\
		.elem_param = 0,					\
		.model = (_model),					\
		.u.sub = { .sub_addr = (_sub_addr) },			\
	}

#define PUB(_elem_off, _model, _app_idx, _addr, _ttl, _period, _xmit)	\
	{								\
		.op = MESH_OP_PUB_SET,				\
		.elem_sel = MESH_ELEM_ADDR_OFF,			\
		.elem_param = (_elem_off),				\
		.model = (_model),					\
		.app_idx = (_app_idx),				\
		.u.pub = {						\
			.pub_addr = (_addr),				\
			.cred_flag = false,				\
			.ttl = (_ttl),					\
			.period = (_period),				\
			.transmit = (_xmit),				\
		},							\
	}

#define PUB_ALL(_model, _app_idx, _addr, _ttl, _period, _xmit)		\
	{								\
		.op = MESH_OP_PUB_SET,				\
		.elem_sel = MESH_ELEM_ALL_WITH_MOD,			\
		.elem_param = 0,					\
		.model = (_model),					\
		.app_idx = (_app_idx),				\
		.u.pub = {						\
			.pub_addr = (_addr),				\
			.cred_flag = false,				\
			.ttl = (_ttl),					\
			.period = (_period),				\
			.transmit = (_xmit),				\
		},							\
	}

/* ------------------------------------------------------------------ */
/* Recipe descriptor                                                   */
/* ------------------------------------------------------------------ */

struct mesh_recipe {
	const struct mesh_setup_step *steps;
	size_t step_cnt;
};

#define RECIPE_ENTRY(_steps_arr) \
	{ .steps = (_steps_arr), .step_cnt = ARRAY_SIZE(_steps_arr) }

/* ------------------------------------------------------------------ */
/* Scenario row                                                        */
/* ------------------------------------------------------------------ */

enum scenario_match_type {
	SCENARIO_MATCH_UUID,
	SCENARIO_MATCH_NAME,
};

/*
 * Rotating rows (NODE_NAME_ROTATE): recipe_ids points to a static array of recipe IDs and
 * recipe_cnt holds its length. recipe_idx advances circularly only after that recipe was
 * successfully applied to a node (executor consumes the current slot).
 * recipe_ids == NULL means the single recipe_id field is used (standard behaviour).
 * The scenario array must NOT be declared const when any rotating row is present, since
 * recipe_idx is mutated at runtime.
 */
struct scenario_row {
	const char    *match;
	uint8_t        match_type;
	uint8_t        recipe_id;   /* single-recipe path; used when recipe_ids == NULL */
	const uint8_t *recipe_ids;  /* rotating recipe list; NULL for non-rotating rows */
	uint8_t        recipe_cnt;  /* length of recipe_ids[]                           */
	uint8_t        recipe_idx;  /* mutable: current rotation position               */
};

#define NODE_CFG_UUID(_uuid_hex, _recipe_id)					\
	{									\
		.match      = (_uuid_hex),					\
		.match_type = SCENARIO_MATCH_UUID,				\
		.recipe_id  = (uint8_t)(_recipe_id),				\
	}

#define NODE_CFG_NAME(_name, _recipe_id)					\
	{									\
		.match      = (_name),						\
		.match_type = SCENARIO_MATCH_NAME,				\
		.recipe_id  = (uint8_t)(_recipe_id),				\
	}

/*
 * NODE_NAME_ROTATE: match by exact device name and assign recipes in a circular sequence.
 * Each time a device with this name finishes successful scenario configuration, the next
 * recipe in the list becomes current for the following device.
 * Usage: NODE_NAME_ROTATE("Name", RECIPE_A, RECIPE_B, ...)
 * The recipe_ids compound literal has static storage duration when used at file scope.
 */
#define NODE_NAME_ROTATE(_name, ...)						\
	{									\
		.match      = (_name),						\
		.match_type = SCENARIO_MATCH_NAME,				\
		.recipe_ids = (const uint8_t[]){ __VA_ARGS__ },			\
		.recipe_cnt = sizeof((uint8_t[]){ __VA_ARGS__ }),		\
	}

#define NODE_FULL(_uuid_hex, _recipe_id) NODE_CFG_UUID((_uuid_hex), (_recipe_id))

#define NODE_PREFIX(_uuid_hex, _recipe_id) NODE_CFG_UUID((_uuid_hex), (_recipe_id))

#define NODE_NAME(_name, _recipe_id) NODE_CFG_NAME((_name), (_recipe_id))

#endif /* MESH_CONFIG_TYPES_H_ */
