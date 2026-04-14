/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "mesh_net_cfg.h"

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/mesh/cdb.h>
#include <zephyr/bluetooth/mesh/cfg_cli.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include "mesh_recipes.h"

LOG_MODULE_REGISTER(mesh_net_cfg, LOG_LEVEL_DBG);

#define MESH_CFG_STATUS_SUCCESS 0x00U
#define MESH_CFG_STATUS_IDX_ALREADY_STORED 0x06U

static uint8_t s_comp_page[BT_MESH_RX_SDU_MAX];
static size_t s_comp_page_len;
static K_MUTEX_DEFINE(scenario_mutex);

static inline bool mesh_cfg_key_add_ok(uint8_t status)
{
	return status == MESH_CFG_STATUS_SUCCESS || status == MESH_CFG_STATUS_IDX_ALREADY_STORED;
}

static const uint8_t *provisioner_net_key_get(uint16_t net_key_idx)
{
	static uint8_t key[16];
	struct bt_mesh_cdb_subnet *sub = bt_mesh_cdb_subnet_get(net_key_idx);

	if (!sub) {
		return NULL;
	}
	if (bt_mesh_cdb_subnet_key_export(sub, 0, key) != 0) {
		return NULL;
	}
	return key;
}

static const uint8_t *provisioner_app_key_get(uint16_t app_key_idx)
{
	static uint8_t key[16];
	struct bt_mesh_cdb_app_key *ak = bt_mesh_cdb_app_key_get(app_key_idx);

	if (!ak) {
		return NULL;
	}
	if (bt_mesh_cdb_app_key_export(ak, 0, key) != 0) {
		return NULL;
	}
	return key;
}

static bool uuid_hex_match(const char *uuid_hex, const uint8_t *dev_uuid)
{
	uint8_t pattern[16];
	size_t hex_len = strlen(uuid_hex);
	size_t match_len = hex_len / 2U;
	size_t decoded;

	if (match_len == 0 || match_len > 16 || (hex_len & 1U)) {
		return false;
	}

	decoded = hex2bin(uuid_hex, hex_len, pattern, match_len);
	if (decoded != match_len) {
		return false;
	}

	return memcmp(pattern, dev_uuid, match_len) == 0;
}

static bool device_name_match(const char *pattern, const char *name)
{
	if (pattern == NULL || pattern[0] == '\0' || name == NULL || name[0] == '\0') {
		return false;
	}

	return strcmp(pattern, name) == 0;
}

static struct scenario_row *scenario_find_row(const uint8_t *dev_uuid,
					      const char *name,
					      struct scenario_row *rows,
					      size_t n_rows)
{
	struct scenario_row *best = NULL;
	size_t best_uuid_len = 0;

	for (size_t i = 0; i < n_rows; i++) {
		if (rows[i].match_type != SCENARIO_MATCH_UUID) {
			continue;
		}

		size_t match_len = strlen(rows[i].match) / 2U;

		if (match_len > best_uuid_len && uuid_hex_match(rows[i].match, dev_uuid)) {
			best = &rows[i];
			best_uuid_len = match_len;
		}
	}

	if (best != NULL) {
		return best;
	}

	for (size_t i = 0; i < n_rows; i++) {
		if (rows[i].match_type != SCENARIO_MATCH_NAME) {
			continue;
		}

		if (device_name_match(rows[i].match, name)) {
			return &rows[i];
		}
	}

	return NULL;
}

/* Returns the current recipe ID without advancing the rotating index. */
static uint8_t scenario_row_peek_recipe_id(const struct scenario_row *row)
{
	if (row->recipe_ids != NULL) {
		return row->recipe_ids[row->recipe_idx];
	}

	return row->recipe_id;
}

/*
 * Consume the current recipe ID for a rotating row after that recipe was
 * successfully applied to the node (bind/sub/pub steps completed).
 */
static void scenario_row_consume_recipe_id(struct scenario_row *row, uint8_t recipe_id)
{
	if (row->recipe_ids == NULL) {
		return;
	}

	if (scenario_row_peek_recipe_id(row) != recipe_id) {
		return;
	}

	row->recipe_idx = (row->recipe_idx + 1) % row->recipe_cnt;
}

static bool comp_elem_has_model(uint16_t primary, uint16_t elem_addr,
				const struct mesh_model_ref *model)
{
	struct net_buf_simple page_buf;
	struct bt_mesh_comp_p0 comp;
	struct bt_mesh_comp_p0_elem elem;
	size_t idx;
	int err;

	if (elem_addr < primary) {
		return false;
	}

	idx = (size_t)(elem_addr - primary);
	if (idx >= MESH_CONFIG_MAX_ELEM_PER_NODE) {
		return false;
	}

	net_buf_simple_init_with_data(&page_buf, s_comp_page, s_comp_page_len);
	err = bt_mesh_comp_p0_get(&comp, &page_buf);
	if (err) {
		return false;
	}

	for (size_t i = 0; i < idx; i++) {
		if (!bt_mesh_comp_p0_elem_pull(&comp, &elem)) {
			return false;
		}
	}

	if (!bt_mesh_comp_p0_elem_pull(&comp, &elem)) {
		return false;
	}

	if (model->cid == MESH_CID_NVAL) {
		for (int s = 0; s < elem.nsig; s++) {
			if (bt_mesh_comp_p0_elem_mod(&elem, s) == model->id) {
				return true;
			}
		}
	} else {
		for (int v = 0; v < elem.nvnd; v++) {
			struct bt_mesh_mod_id_vnd vid = bt_mesh_comp_p0_elem_mod_vnd(&elem, v);

			if (vid.id == model->id && vid.company == model->cid) {
				return true;
			}
		}
	}

	return false;
}

static int comp_data_fetch(struct bt_mesh_cdb_node *node, uint16_t *elem_addrs,
			   size_t elem_addrs_cap, size_t *n_elems_out)
{
	NET_BUF_SIMPLE_DEFINE(buf, BT_MESH_RX_SDU_MAX);
	struct net_buf_simple page_buf;
	uint8_t page_rsp;
	struct bt_mesh_comp_p0 comp;
	struct bt_mesh_comp_p0_elem elem;
	size_t n_elem;
	int err;

	*n_elems_out = 0;

	err = bt_mesh_cfg_cli_comp_data_get(node->net_idx, node->addr, 0, &page_rsp, &buf);
	if (err) {
		LOG_ERR("comp data get failed (err %d)", err);
		return err;
	}

	if (page_rsp != 0U) {
		LOG_ERR("unexpected composition page %u", page_rsp);
		return -EIO;
	}

	if (buf.len > sizeof(s_comp_page)) {
		LOG_ERR("composition page too large");
		return -ENOMEM;
	}

	memcpy(s_comp_page, buf.data, buf.len);
	s_comp_page_len = buf.len;

	net_buf_simple_init_with_data(&page_buf, s_comp_page, s_comp_page_len);
	err = bt_mesh_comp_p0_get(&comp, &page_buf);
	if (err) {
		LOG_ERR("comp parse failed (err %d)", err);
		return err;
	}

	n_elem = 0;
	while (bt_mesh_comp_p0_elem_pull(&comp, &elem)) {
		if (n_elem >= elem_addrs_cap) {
			LOG_ERR("composition element count exceeds configured maximum");
			return -ENOMEM;
		}
		elem_addrs[n_elem] = (uint16_t)(node->addr + n_elem);
		n_elem++;
	}

	*n_elems_out = n_elem;
	return 0;
}

static int apply_step(struct bt_mesh_cdb_node *node, const struct mesh_setup_step *step,
		      const uint16_t *elem_addrs, size_t n_elems)
{
	uint16_t primary = node->addr;
	uint16_t net_idx = node->net_idx;
	uint8_t status = MESH_CFG_STATUS_SUCCESS;
	int err = 0;

	uint16_t addrs[MESH_CONFIG_MAX_ELEM_PER_NODE];
	size_t addr_cnt = 0;

	switch (step->elem_sel) {
	case MESH_ELEM_ADDR_OFF:
		if (step->elem_param >= n_elems) {
			LOG_ERR("elem_param %u >= n_elems %zu", step->elem_param, n_elems);
			return -EINVAL;
		}
		addrs[0] = elem_addrs[step->elem_param];
		addr_cnt = 1;
		break;
	case MESH_ELEM_ALL_WITH_MOD:
		for (size_t i = 0; i < n_elems; i++) {
			if (!comp_elem_has_model(primary, elem_addrs[i], &step->model)) {
				continue;
			}
			if (addr_cnt >= MESH_CONFIG_MAX_ELEM_PER_NODE) {
				LOG_ERR("ALL_WITH_MOD: element overflow");
				return -ENOMEM;
			}
			addrs[addr_cnt++] = elem_addrs[i];
		}
		break;
	default:
		return -ENOTSUP;
	}

	for (size_t i = 0; i < addr_cnt && !err; i++) {
		uint16_t ea = addrs[i];
		bool sig = (step->model.cid == MESH_CID_NVAL);

		switch (step->op) {
		case MESH_OP_NETKEY_ADD: {
			const uint8_t *key = provisioner_net_key_get(step->u.netkey.net_key_idx);

			if (!key) {
				err = -ENOENT;
				break;
			}
			LOG_INF("bt_mesh_cfg_cli_net_key_add dst 0x%04x net_idx=%u net_key_idx=%u",
				primary, net_idx, step->u.netkey.net_key_idx);
			err = bt_mesh_cfg_cli_net_key_add(net_idx, primary,
							  step->u.netkey.net_key_idx, key, &status);
			if (!err && !mesh_cfg_key_add_ok(status)) {
				err = -EIO;
			}
			break;
		}
		case MESH_OP_APPKEY_ADD: {
			const uint8_t *key = provisioner_app_key_get(step->u.appkey.app_key_idx);

			if (!key) {
				err = -ENOENT;
				break;
			}
			LOG_INF("bt_mesh_cfg_cli_app_key_add dst 0x%04x net_idx=%u net_key_idx=%u "
				"app_key_idx=%u",
				primary, net_idx, step->u.appkey.net_key_idx,
				step->u.appkey.app_key_idx);
			err = bt_mesh_cfg_cli_app_key_add(net_idx, primary,
							  step->u.appkey.net_key_idx,
							  step->u.appkey.app_key_idx, key, &status);
			if (!err && !mesh_cfg_key_add_ok(status)) {
				err = -EIO;
			}
			break;
		}
		case MESH_OP_BIND_APP:
			if (sig) {
				LOG_INF("bt_mesh_cfg_cli_mod_app_bind dst 0x%04x net_idx=%u elem 0x%04x "
					"app_idx=%u model_id=0x%04x",
					primary, net_idx, ea, step->app_idx, step->model.id);
				err = bt_mesh_cfg_cli_mod_app_bind(net_idx, primary, ea, step->app_idx,
								   step->model.id, &status);
			} else {
				LOG_INF("bt_mesh_cfg_cli_mod_app_bind_vnd dst 0x%04x net_idx=%u "
					"elem 0x%04x app_idx=%u model_id=0x%04x company=0x%04x",
					primary, net_idx, ea, step->app_idx, step->model.id,
					step->model.cid);
				err = bt_mesh_cfg_cli_mod_app_bind_vnd(net_idx, primary, ea,
								       step->app_idx, step->model.id,
								       step->model.cid, &status);
			}
			if (!err && status != MESH_CFG_STATUS_SUCCESS) {
				err = -EIO;
			}
			break;
		case MESH_OP_SUB_ADD:
			if (sig) {
				LOG_INF("bt_mesh_cfg_cli_mod_sub_add dst 0x%04x net_idx=%u elem 0x%04x "
					"group 0x%04x model_id=0x%04x",
					primary, net_idx, ea, step->u.sub.sub_addr, step->model.id);
				err = bt_mesh_cfg_cli_mod_sub_add(net_idx, primary, ea,
								  step->u.sub.sub_addr,
								  step->model.id, &status);
			} else {
				LOG_INF("bt_mesh_cfg_cli_mod_sub_add_vnd dst 0x%04x net_idx=%u "
					"elem 0x%04x group 0x%04x model_id=0x%04x company=0x%04x",
					primary, net_idx, ea, step->u.sub.sub_addr, step->model.id,
					step->model.cid);
				err = bt_mesh_cfg_cli_mod_sub_add_vnd(net_idx, primary, ea,
								      step->u.sub.sub_addr,
								      step->model.id,
								      step->model.cid, &status);
			}
			if (!err && status != MESH_CFG_STATUS_SUCCESS) {
				err = -EIO;
			}
			break;
		case MESH_OP_PUB_SET: {
			struct bt_mesh_cfg_cli_mod_pub pub = {
				.addr = step->u.pub.pub_addr,
				.uuid = NULL,
				.app_idx = step->app_idx,
				.cred_flag = step->u.pub.cred_flag,
				.ttl = step->u.pub.ttl,
				.period = step->u.pub.period,
				.transmit = step->u.pub.transmit,
			};

			if (sig) {
				LOG_INF("bt_mesh_cfg_cli_mod_pub_set dst 0x%04x net_idx=%u elem 0x%04x "
					"model_id=0x%04x pub_addr=0x%04x app_idx=%u ttl=%u period=%u "
					"cred_flag=%u transmit=0x%02x",
					primary, net_idx, ea, step->model.id, pub.addr, pub.app_idx,
					pub.ttl, pub.period, (unsigned int)pub.cred_flag, pub.transmit);
				err = bt_mesh_cfg_cli_mod_pub_set(net_idx, primary, ea, step->model.id,
								  &pub, &status);
			} else {
				LOG_INF("bt_mesh_cfg_cli_mod_pub_set_vnd dst 0x%04x net_idx=%u "
					"elem 0x%04x model_id=0x%04x company=0x%04x pub_addr=0x%04x "
					"app_idx=%u ttl=%u period=%u cred_flag=%u transmit=0x%02x",
					primary, net_idx, ea, step->model.id, step->model.cid, pub.addr,
					pub.app_idx, pub.ttl, pub.period, (unsigned int)pub.cred_flag,
					pub.transmit);
				err = bt_mesh_cfg_cli_mod_pub_set_vnd(net_idx, primary, ea,
								      step->model.id,
								      step->model.cid, &pub,
								      &status);
			}
			if (!err && status != MESH_CFG_STATUS_SUCCESS) {
				err = -EIO;
			}
			break;
		}
		default:
			return -ENOTSUP;
		}
	}

	return err;
}

int mesh_config_apply_recipe(struct bt_mesh_cdb_node *node, uint8_t recipe_id)
{
	const struct mesh_recipe *recipe;
	uint16_t elem_addrs[MESH_CONFIG_MAX_ELEM_PER_NODE];
	size_t n_elems = 0;
	int err;

	if (recipe_id == RECIPE_NONE || recipe_id >= RECIPE_COUNT) {
		return -ENOENT;
	}

	recipe = &mesh_recipes[recipe_id];

	err = comp_data_fetch(node, elem_addrs, ARRAY_SIZE(elem_addrs), &n_elems);
	if (err) {
		return err;
	}

	if (n_elems != node->num_elem) {
		LOG_ERR("composition n_elems %zu != CDB num_elem %u", n_elems, node->num_elem);
		return -EINVAL;
	}

	for (size_t s = 0; s < recipe->step_cnt; s++) {
		err = apply_step(node, &recipe->steps[s], elem_addrs, n_elems);
		if (err) {
			LOG_ERR("Apply failed at step %zu/%zu (err %d)", s + 1, recipe->step_cnt, err);
			return err;
		}
	}

	return 0;
}

int mesh_config_apply(struct bt_mesh_cdb_node *node, struct scenario_row *rows,
		      size_t n_rows, const char *name)
{
	struct scenario_row *row;
	uint8_t recipe_id;
	int err;

	k_mutex_lock(&scenario_mutex, K_FOREVER);
	row = scenario_find_row(node->uuid, name, rows, n_rows);
	if (!row) {
		k_mutex_unlock(&scenario_mutex);
		return -ENOENT;
	}

	recipe_id = scenario_row_peek_recipe_id(row);
	k_mutex_unlock(&scenario_mutex);
	err = mesh_config_apply_recipe(node, recipe_id);
	if (!err) {
		k_mutex_lock(&scenario_mutex, K_FOREVER);
		scenario_row_consume_recipe_id(row, recipe_id);
		k_mutex_unlock(&scenario_mutex);
	}

	return err;
}

static const char *const recipe_names[RECIPE_COUNT] = {
	[RECIPE_NONE] = "none",
	[RECIPE_LIGHT_CTRL_1] = "light_ctrl_1",
	[RECIPE_LIGHT_CTRL_2] = "light_ctrl_2",
	[RECIPE_DIMMER_1] = "dimmer_1",
	[RECIPE_DIMMER_2] = "dimmer_2",
	[RECIPE_LIGHT_SW_1] = "light_sw_1",
	[RECIPE_LIGHT_SW_2] = "light_sw_2",
	[RECIPE_ENOCEAN_BCAST] = "enocean_bcast",
};

const char *mesh_recipe_name(uint8_t recipe_id)
{
	if (recipe_id >= RECIPE_COUNT || !recipe_names[recipe_id]) {
		return "unknown";
	}
	return recipe_names[recipe_id];
}

void mesh_scenario_consume_recipe_id(const uint8_t uuid[16], const char *name,
				     struct scenario_row *rows, size_t n_rows,
				     uint8_t recipe_id)
{
	struct scenario_row *row = scenario_find_row(uuid, name, rows, n_rows);

	k_mutex_lock(&scenario_mutex, K_FOREVER);
	if (row != NULL) {
		scenario_row_consume_recipe_id(row, recipe_id);
	}
	k_mutex_unlock(&scenario_mutex);
}

uint8_t mesh_scenario_lookup_recipe_id(const uint8_t uuid[16], const char *name,
				       struct scenario_row *rows, size_t n_rows)
{
	struct scenario_row *row = scenario_find_row(uuid, name, rows, n_rows);
	uint8_t id;

	k_mutex_lock(&scenario_mutex, K_FOREVER);
	if (!row) {
		k_mutex_unlock(&scenario_mutex);
		return RECIPE_NONE;
	}

	id = scenario_row_peek_recipe_id(row);
	k_mutex_unlock(&scenario_mutex);

	if (id == RECIPE_NONE || id >= RECIPE_COUNT) {
		return RECIPE_NONE;
	}

	return id;
}
