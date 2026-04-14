/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/net_buf.h>

#include "prov_cache.h"
#include "prov_scan_names.h"
#include "prov_shell.h"

/* Zephyr mesh internal; links with Bluetooth Mesh subsystem. */
extern int bt_mesh_scan_active_set(bool active);

static void parse_adv_ind_mesh_prov(const struct bt_le_scan_recv_info *info,
				    struct net_buf_simple *buf)
{
	struct net_buf_simple_state state;

	while (buf->len > 1) {
		uint8_t len;
		uint8_t type;

		len = net_buf_simple_pull_u8(buf);
		if (len == 0U) {
			return;
		}

		if (len > buf->len) {
			return;
		}

		net_buf_simple_save(buf, &state);

		type = net_buf_simple_pull_u8(buf);
		buf->len = len - 1;

		if (type == BT_DATA_SVC_DATA16 && buf->len >= 20) {
			uint16_t u = net_buf_simple_pull_le16(buf);

			if (u == BT_UUID_MESH_PROV_VAL && buf->len >= 18) {
				const uint8_t *uuid = net_buf_simple_pull_mem(buf, 16);

				if (prov_cache_adv_uuid_seen(info->addr, uuid) == 0) {
					prov_shell_note_uuid_update(uuid);
				}
			}
		}

		net_buf_simple_restore(buf, &state);
		net_buf_simple_pull(buf, len);
	}
}

static void parse_scan_rsp_name(const struct bt_le_scan_recv_info *info,
				struct net_buf_simple *buf)
{
	struct net_buf_simple_state state;

	while (buf->len > 1) {
		uint8_t len;
		uint8_t type;

		len = net_buf_simple_pull_u8(buf);
		if (len == 0U) {
			return;
		}

		if (len > buf->len) {
			return;
		}

		net_buf_simple_save(buf, &state);

		type = net_buf_simple_pull_u8(buf);
		buf->len = len - 1;

		if ((type == BT_DATA_NAME_COMPLETE || type == BT_DATA_NAME_SHORTENED) &&
		    buf->len > 0) {
			uint8_t uuid[16];

			if (prov_cache_scan_rsp_name_seen(info->addr, buf->data, buf->len,
							  uuid)) {
				prov_shell_note_uuid_update(uuid);
			}
		}

		net_buf_simple_restore(buf, &state);
		net_buf_simple_pull(buf, len);
	}
}

static void prov_scan_recv(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
	if (info->addr == NULL) {
		return;
	}

	if (info->adv_type == BT_GAP_ADV_TYPE_ADV_IND) {
		parse_adv_ind_mesh_prov(info, buf);
		return;
	}

	if (info->adv_type == BT_GAP_ADV_TYPE_SCAN_RSP) {
		parse_scan_rsp_name(info, buf);
	}
}

static struct bt_le_scan_cb scan_cb = {
	.recv = prov_scan_recv,
};

void prov_scan_names_init(void)
{
	bt_le_scan_cb_register(&scan_cb);
}

int prov_scan_names_set_mesh_active_scan(bool active)
{
	return bt_mesh_scan_active_set(active);
}
