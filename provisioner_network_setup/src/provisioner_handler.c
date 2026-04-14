/*
 * Copyright (c) 2019 Tobias Svehagen
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/mesh/cdb.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "provisioner_handler.h"
#include "prov_cache.h"
#include "prov_scan_names.h"
#include "prov_shell.h"
#include "mesh_net_cfg.h"
#include "mesh_recipes.h"
#include "scenario_network1.h"

#define PROVISIONER_THREAD_PRIO 7

/*
 * Attention duration (seconds) passed to bt_mesh_provision_adv. A non-zero value causes the
 * provisionee to blink / signal attention during the provisioning exchange, providing visual
 * feedback on the target device.
 */
#define PROVISION_ATTENTION_SEC 5

static const uint16_t net_idx;
static const uint16_t app_idx;
static uint16_t self_addr = 1, node_addr;
static const uint8_t dev_uuid[16] = { 0xdd, 0xdd };
static uint8_t node_uuid[16];
static uint8_t pending_recipe_id = RECIPE_NONE;
static uint8_t session_recipe_id = RECIPE_NONE;

/*
 * Persisted across reboot: last provisioned node address and whether its
 * scenario configuration must still be completed. Cleared when that node's
 * configuration succeeds.
 */
static struct {
	uint16_t recent_new_addr;
	uint8_t config_pending;
	uint8_t recipe_id;
} prov_pending;

static int mesh_prov_settings_set(const char *name, size_t len, settings_read_cb read_cb,
				  void *cb_arg);
static int mesh_prov_settings_export(int (*storage_func)(const char *name, const void *value,
							 size_t val_len));

SETTINGS_STATIC_HANDLER_DEFINE(mesh_prov_cfg, "mesh_prov", NULL, mesh_prov_settings_set, NULL,
			       mesh_prov_settings_export);

static void prov_pending_save(void)
{
	int err;

	if (!IS_ENABLED(CONFIG_SETTINGS)) {
		return;
	}

	err = settings_save_one("mesh_prov/cfg", &prov_pending, sizeof(prov_pending));
	if (err) {
		printk("prov: settings_save_one failed (%d)\n", err);
	}
}

static int mesh_prov_settings_set(const char *name, size_t len, settings_read_cb read_cb,
				  void *cb_arg)
{
	struct {
		uint16_t recent_new_addr;
		uint8_t config_pending;
	} prov_pending_v1;
	ssize_t r;

	if (!name || !settings_name_steq(name, "cfg", NULL)) {
		return -ENOENT;
	}

	if (len == sizeof(prov_pending)) {
		r = read_cb(cb_arg, &prov_pending, sizeof(prov_pending));
		if (r < 0) {
			return (int)r;
		}

		return 0;
	}

	if (len != sizeof(prov_pending_v1)) {
		return -EINVAL;
	}

	r = read_cb(cb_arg, &prov_pending_v1, sizeof(prov_pending_v1));
	if (r < 0) {
		return (int)r;
	}

	prov_pending.recent_new_addr = prov_pending_v1.recent_new_addr;
	prov_pending.config_pending = prov_pending_v1.config_pending;
	prov_pending.recipe_id = RECIPE_NONE;

	return 0;
}

static int mesh_prov_settings_export(int (*storage_func)(const char *name, const void *value,
							 size_t val_len))
{
	return storage_func("mesh_prov/cfg", &prov_pending, sizeof(prov_pending));
}

K_SEM_DEFINE(sem_provision_request, 0, 1);
K_SEM_DEFINE(sem_node_added, 0, 1);
static K_MUTEX_DEFINE(prov_pending_mutex);
static uint8_t pending_prov_uuid[16];
static atomic_t handler_ready_flag;
static atomic_t provision_busy_flag;

static struct bt_mesh_cfg_cli cfg_cli;

static void health_current_status(struct bt_mesh_health_cli *cli, uint16_t addr,
				  uint8_t test_id, uint16_t cid, uint8_t *faults,
				  size_t fault_count)
{
	size_t i;

	printk("Health Current Status from 0x%04x\n", addr);

	if (!fault_count) {
		printk("Health Test ID 0x%02x Company ID 0x%04x: no faults\n", test_id, cid);
		return;
	}

	printk("Health Test ID 0x%02x Company ID 0x%04x Fault Count %zu:\n", test_id, cid,
		fault_count);

	for (i = 0; i < fault_count; i++) {
		printk("\t0x%02x\n", faults[i]);
	}
}

static struct bt_mesh_health_cli health_cli = {
	.current_status = health_current_status,
};

static const struct bt_mesh_model root_models[] = {
	BT_MESH_MODEL_CFG_SRV,
	BT_MESH_MODEL_CFG_CLI(&cfg_cli),
	BT_MESH_MODEL_HEALTH_CLI(&health_cli),
};

static const struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(0, root_models, BT_MESH_MODEL_NONE),
};

static const struct bt_mesh_comp mesh_comp = {
	.cid = BT_COMP_ID_LF,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

static void setup_cdb(void)
{
	struct bt_mesh_cdb_app_key *key;
	uint8_t app_key[16];
	int err;

	key = bt_mesh_cdb_app_key_alloc(net_idx, app_idx);
	if (key == NULL) {
		printk("Failed to allocate app-key 0x%04x\n", app_idx);
		return;
	}

	bt_rand(app_key, 16);

	err = bt_mesh_cdb_app_key_import(key, 0, app_key);
	if (err) {
		printk("Failed to import appkey into cdb. Err:%d\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		bt_mesh_cdb_app_key_store(key);
	}
}

static void configure_self(struct bt_mesh_cdb_node *self)
{
	struct bt_mesh_cdb_app_key *key;
	uint8_t app_key[16];
	uint8_t status = 0;
	int err;

	printk("Configuring self...\n");

	key = bt_mesh_cdb_app_key_get(app_idx);
	if (key == NULL) {
		printk("No app-key 0x%04x\n", app_idx);
		return;
	}

	err = bt_mesh_cdb_app_key_export(key, 0, app_key);
	if (err) {
		printk("Failed to export appkey from cdb. Err:%d\n", err);
		return;
	}

	/* Add Application Key */
	err = bt_mesh_cfg_cli_app_key_add(self->net_idx, self->addr,
					  self->net_idx, app_idx, app_key, &status);
	if (err || status) {
		printk("Failed to add app-key (err %d, status %d)\n", err, status);
		return;
	}

	err = bt_mesh_cfg_cli_mod_app_bind(self->net_idx, self->addr, self->addr,
					   app_idx, BT_MESH_MODEL_ID_HEALTH_CLI, &status);
	if (err || status) {
		printk("Failed to bind app-key (err %d, status %d)\n", err, status);
		return;
	}

	atomic_set_bit(self->flags, BT_MESH_CDB_NODE_CONFIGURED);

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		bt_mesh_cdb_node_store(self);
	}

	printk("Configuration complete\n");
}

static void configure_node(struct bt_mesh_cdb_node *node)
{
	int err;
	char uuid_hex[32 + 1];
	char name[PROV_CACHE_NAME_BUF_SIZE];
	const char *node_name = NULL;
	uint8_t recipe_id = RECIPE_NONE;

	printk("Configuring node 0x%04x...\n", node->addr);

	if (prov_cache_lookup_name(node->uuid, name, sizeof(name))) {
		node_name = name;
	}

	if (prov_pending.config_pending && node->addr == prov_pending.recent_new_addr) {
		recipe_id = prov_pending.recipe_id;
	} else if (node->addr == node_addr) {
		recipe_id = session_recipe_id;
	}

	if (recipe_id != RECIPE_NONE) {
		err = mesh_config_apply_recipe(node, recipe_id);
	} else {
		err = mesh_config_apply(node, scenario_b1_corridor, scenario_b1_corridor_len,
					node_name);
	}
	if (err == -ENOENT) {
		if (prov_shell_auto_provision_enabled()) {
			bin2hex(node->uuid, 16, uuid_hex, sizeof(uuid_hex));
			if (node_name != NULL) {
				printk("Beacon: %s (%s) -> recipe 'none'\n", uuid_hex, node_name);
			} else {
				printk("Beacon: %s -> recipe 'none'\n", uuid_hex);
			}
		} else {
			printk("No recipe for UUID (see scenario_network1)\n");
		}
		return;
	}
	if (err) {
		printk("Node configuration failed (err %d)\n", err);
		return;
	}

	/*
	 * Advance the rotating recipe index now that configuration succeeded.
	 * The recipe_id != RECIPE_NONE branch reaches here only for auto or
	 * explicit-recipe requests; the else branch calls mesh_config_apply()
	 * which consumes the index internally on success.
	 */
	if (recipe_id != RECIPE_NONE) {
		mesh_scenario_consume_recipe_id(node->uuid, node_name, scenario_b1_corridor,
						scenario_b1_corridor_len, recipe_id);
	}

	atomic_set_bit(node->flags, BT_MESH_CDB_NODE_CONFIGURED);

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		bt_mesh_cdb_node_store(node);
	}

	if (prov_pending.config_pending && node->addr == prov_pending.recent_new_addr) {
		prov_pending.recent_new_addr = 0U;
		prov_pending.config_pending = 0U;
		prov_pending.recipe_id = RECIPE_NONE;
		prov_pending_save();
	}

	prov_cache_on_configuration_success(node->uuid);

	printk("Configuration complete\n");
	prov_shell_auto_retry_submit();
}

static void unprovisioned_beacon(uint8_t uuid[16], bt_mesh_prov_oob_info_t oob_info,
				 uint32_t *uri_hash)
{
	ARG_UNUSED(oob_info);
	ARG_UNUSED(uri_hash);

	prov_shell_feed_beacon(uuid);
}

static void node_added(uint16_t idx, uint8_t uuid[16], uint16_t addr, uint8_t num_elem)
{
	ARG_UNUSED(idx);
	ARG_UNUSED(num_elem);

	prov_pending.recent_new_addr = addr;
	prov_pending.config_pending = 1U;
	prov_pending.recipe_id = session_recipe_id;
	prov_pending_save();

	node_addr = addr;
	k_sem_give(&sem_node_added);
}

/*
 * bt_mesh_prov for the provisioner. No static OOB material is set; output_size and
 * input_size are both zero, which selects No-OOB authentication. This matches the
 * "No-OOB" requirement in the plan and avoids stalling on devices that also use No-OOB.
 */
static const struct bt_mesh_prov prov = {
	.uuid = dev_uuid,
	.unprovisioned_beacon = unprovisioned_beacon,
	.node_added = node_added,
};

const struct bt_mesh_prov *provisioner_handler_prov(void)
{
	return &prov;
}

const struct bt_mesh_comp *provisioner_handler_init(void)
{
	return &mesh_comp;
}

static void provisioner_resume_pending_config(void)
{
	struct bt_mesh_cdb_node *node;

	if (!prov_pending.config_pending || prov_pending.recent_new_addr == 0U) {
		return;
	}

	node = bt_mesh_cdb_node_get(prov_pending.recent_new_addr);
	if (!node) {
		printk("provisioner: pending config 0x%04x not in CDB, clearing\n",
		       prov_pending.recent_new_addr);
		prov_pending.recent_new_addr = 0U;
		prov_pending.config_pending = 0U;
		prov_pending.recipe_id = RECIPE_NONE;
		prov_pending_save();
		return;
	}

	if (atomic_test_bit(node->flags, BT_MESH_CDB_NODE_CONFIGURED)) {
		prov_pending.recent_new_addr = 0U;
		prov_pending.config_pending = 0U;
		prov_pending.recipe_id = RECIPE_NONE;
		prov_pending_save();
		return;
	}

	printk("provisioner: resuming configuration for 0x%04x after reboot\n", node->addr);
	configure_node(node);
}

static int provisioner_network_setup(void)
{
	uint8_t net_key[16], dev_key[16];
	int err;

	err = bt_mesh_init(provisioner_handler_prov(), provisioner_handler_init());
	if (err) {
		printk("Initializing mesh failed (err %d)\n", err);
		return err;
	}

	printk("Mesh initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		printk("Loading stored settings\n");
		settings_load();
	}

	bt_rand(net_key, 16);

	err = bt_mesh_cdb_create(net_key);
	if (err == -EALREADY) {
		printk("Using stored CDB\n");
	} else if (err) {
		printk("Failed to create CDB (err %d)\n", err);
		return err;
	} else {
		printk("Created CDB\n");
		setup_cdb();
	}

	bt_rand(dev_key, 16);

	err = bt_mesh_provision(net_key, BT_MESH_NET_PRIMARY, 0, 0, self_addr,
				dev_key);
	if (err == -EALREADY) {
		printk("Using stored settings\n");
	} else if (err) {
		printk("Provisioning failed (err %d)\n", err);
		return err;
	} else {
		printk("Provisioning completed\n");
	}

	provisioner_resume_pending_config();

	return 0;
}

static uint8_t check_unconfigured(struct bt_mesh_cdb_node *node, void *data)
{
	if (!atomic_test_bit(node->flags, BT_MESH_CDB_NODE_CONFIGURED)) {
		if (node->addr == self_addr) {
			configure_self(node);
		} else {
			configure_node(node);
		}
	}

	return BT_MESH_CDB_ITER_CONTINUE;
}

static void provisioner_thread(void *p1, void *p2, void *p3)
{
	char uuid_hex_str[32 + 1];
	int err;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		k_sem_reset(&sem_node_added);
		bt_mesh_cdb_node_foreach(check_unconfigured, NULL);

		/* All pending configurations (from check_unconfigured) are finished. */
		atomic_set(&provision_busy_flag, 0);

		/*
		 * Both manual (prov provision) and auto mode converge here.
		 * Auto mode triggers sem_provision_request either directly from
		 * prov_shell_feed_beacon on incoming beacons or from the one-shot
		 * retry work after configuration completes. Manual mode triggers it
		 * from the shell command handler. Either way the thread wakes up,
		 * grabs the UUID that was latched into pending_prov_uuid, and
		 * provisions it.
		 */
		k_sem_take(&sem_provision_request, K_FOREVER);

		k_mutex_lock(&prov_pending_mutex, K_FOREVER);
		memcpy(node_uuid, pending_prov_uuid, 16);
		session_recipe_id = pending_recipe_id;
		k_mutex_unlock(&prov_pending_mutex);

		bin2hex(node_uuid, 16, uuid_hex_str, sizeof(uuid_hex_str));
		{
			char nm[PROV_CACHE_NAME_BUF_SIZE];

			if (prov_cache_lookup_name(node_uuid, nm, sizeof(nm))) {
				printk("Provisioning %s (%s)\n", uuid_hex_str, nm);
			} else {
				printk("Provisioning %s\n", uuid_hex_str);
			}
		}

		err = bt_mesh_provision_adv(node_uuid, net_idx, 0, PROVISION_ATTENTION_SEC);
		if (err < 0) {
			printk("Provisioning failed (err %d)\n", err);
			atomic_set(&provision_busy_flag, 0);
			continue;
		}

		printk("Waiting for node to be added...\n");
		err = k_sem_take(&sem_node_added, K_SECONDS(10));
		if (err == -EAGAIN) {
			printk("Timeout waiting for node to be added\n");
			atomic_set(&provision_busy_flag, 0);
			continue;
		}

		printk("Added node 0x%04x\n", node_addr);
		/* Busy flag is cleared after configuration (triggered by sem_node_added)
		 * is finished by the thread loop. */
	}
}

static K_THREAD_STACK_DEFINE(provisioner_stack, CONFIG_MESH_PROVISIONER_THREAD_STACK_SIZE);
static struct k_thread provisioner_thread_data;

int provisioner_handler_request_provision(const uint8_t uuid[16])
{
	return provisioner_handler_request_provision_recipe(uuid, RECIPE_NONE);
}

int provisioner_handler_request_provision_recipe(const uint8_t uuid[16], uint8_t recipe_id)
{
	if (!atomic_get(&handler_ready_flag)) {
		return -ENODEV;
	}

	if (!atomic_cas(&provision_busy_flag, 0, 1)) {
		return -EBUSY;
	}

	k_mutex_lock(&prov_pending_mutex, K_FOREVER);
	memcpy(pending_prov_uuid, uuid, 16);
	pending_recipe_id = recipe_id;
	k_mutex_unlock(&prov_pending_mutex);

	k_sem_give(&sem_provision_request);
	return 0;
}

int provisioner_handler_bluetooth_ready(void)
{
	int err;

	prov_scan_names_init();

	err = provisioner_network_setup();
	if (err) {
		return err;
	}

	err = prov_scan_names_set_mesh_active_scan(true);
	if (err != 0) {
		printk("provisioner: active scan for GAP names failed (%d)\n", err);
	}

	atomic_set(&handler_ready_flag, 1);

	k_thread_create(&provisioner_thread_data, provisioner_stack,
			K_THREAD_STACK_SIZEOF(provisioner_stack), provisioner_thread, NULL, NULL,
			NULL, K_PRIO_COOP(PROVISIONER_THREAD_PRIO), 0, K_NO_WAIT);
	k_thread_name_set(&provisioner_thread_data, "mesh_provisioner");

	return 0;
}
