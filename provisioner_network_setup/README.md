# Bluetooth Mesh provisioner (scenario-based configuration)

> **IMPORTANT**:
> - This sample was prepared using release tag `v3.2.4`. This sample may not work with other releases.
> - You may use this sample as a reference for your own implementation.
> - This sample is not intended to be maintained actively, neither it is thoroughly tested!

## Overview

This sample is a Bluetooth Mesh provisioner: it provisions devices over PB-ADV and applies
deployment-specific rules (model binding, publication, subscription).

Key source files:

- `src/prov_cache.c` and `include/prov_cache.h` - discovery support for shell scan and auto mode.
- `src/mesh_recipes.c` - recipes: which models to bind and how to set pub/sub per node type.
- `include/mesh_recipes.h` - recipe IDs and group address constants.
- `src/scenario_network1.c` - deployment table: which recipe applies to which device (by name or
  UUID prefix).

Provisioning and authentication:

- No-OOB authentication (unprovisioned devices accept the provisioning key).
- Shell-driven: manual `prov provision` or `prov auto on`.

---

## Shell commands: scan, provision, auto

After boot, use the `prov` command group:


| Command                          | Purpose |
| -------------------------------- | ------- |
| `prov auto on` / `prov auto off` | Auto mode: provision using the resolved recipe when enabled. |
| `prov scan`                      | List cached unprovisioned devices (and scan status). |
| `prov scan on` / `prov scan off` | Enable or disable scanning. |
| `prov provision <uuid>`          | Start provisioning one device (32-character hex UUID). |
| `prov provision <index>`         | Start provisioning by index from the `prov scan` list. |

A device appears in `prov scan` only after both its provisioning UUID and GAP name are known; if a
beacon is visible but the name is still missing, wait for active scanning to resolve it.

Typical workflow:

1. Power targets in the unprovisioned state.
2. Run `prov auto on`, or `prov scan on` and then `prov provision <idx>` manually.
3. Watch for following type of messages:
```shell
Provisioning <uuid> (<name>)
Waiting for node to be added...
Added node 0x...
Configuring ...
Configuration complete
```

## Building and flashing

From your NCS workspace (after sourcing the SDK):

```shell
cd mesh_samples/provisioner_network_setup
west build -p -b nrf54l15dk/nrf54l15/cpuapp
west flash --erase
```

Board selection:

- Replace `nrf54l15dk/nrf54l15/cpuapp` with your board (for example `nrf52840dk/nrf52840` or
  `nrf5340dk/nrf5340/cpuapp`).
- Board fragments under `boards/` adjust stacks and storage:
  - nRF54L: ZMS-backed settings (`boards/nrf54l15dk_*_cpuapp.conf`)
  - nRF52: NVS via `prj.conf`


## Running the sample

### Setup and preparation

- Flash one board with `provisioner_network_setup` firmware.
- Flash targets with firmware whose GAP name and/or UUID match `scenario_network1.c`, or edit that
  file for your deployment.
- Keep targets unprovisioned (factory flash or clear mesh settings per their documentation).

### Running the provisioner

1. Power the provisioner and open a serial console.
  - Confirm boot message: `Mesh initialized`
2. Power targets so they advertise as unprovisioned.
3. On the provisioner:
  - `prov auto on`, (Recommended approach) or
  - `prov scan on` and then `prov provision <idx>` (Manual approach).
4. Expect:
  - `Provisioning <uuid> (<name>)` when name matching succeeds.
  - `Configuration complete` when the recipe is applied.

### Example log

Below, `prov auto on` provisions one Mesh Light Switch and two Mesh Light Fixture nodes. Debug
output includes net key, app key, bind, subscription, and publication steps.

```text
uart:~$ prov auto on
Auto provisioning: enabled
Provisioning fa624713356549a1859db8ecca9a065e (Mesh Light Switch)
Waiting for node to be added...
Added node 0x0002
Configuring node 0x0002...
Configuring 0x0002...
I: bt_mesh_cfg_cli_net_key_add dst 0x0002 net_idx=0 net_key_idx=0
I: bt_mesh_cfg_cli_app_key_add dst 0x0002 net_idx=0 net_key_idx=0 app_key_idx=0
I: bt_mesh_cfg_cli_mod_app_bind dst 0x0002 net_idx=0 elem 0x0002 app_idx=0 model_id=0x1001
I: bt_mesh_cfg_cli_mod_pub_set dst 0x0002 net_idx=0 elem 0x0002 model_id=0x1001 pub_addr=0xc001 app_idx=0 ttl=5 period=0 cred_flag=0 transmit=0x09
Configuration complete
Provisioning ada8e945ff034ae4925716ba00fc451b (Mesh Light Fixture)
Waiting for node to be added...
Added node 0x0006
Configuring node 0x0006...
Configuring 0x0006...
I: bt_mesh_cfg_cli_net_key_add dst 0x0006 net_idx=0 net_key_idx=0
I: bt_mesh_cfg_cli_app_key_add dst 0x0006 net_idx=0 net_key_idx=0 app_key_idx=0
I: bt_mesh_cfg_cli_mod_app_bind dst 0x0006 net_idx=0 elem 0x0006 app_idx=0 model_id=0x1300
I: bt_mesh_cfg_cli_mod_sub_add dst 0x0006 net_idx=0 elem 0x0006 group 0xc001 model_id=0x1300
I: bt_mesh_cfg_cli_mod_app_bind dst 0x0006 net_idx=0 elem 0x0007 app_idx=0 model_id=0x130f
I: bt_mesh_cfg_cli_mod_app_bind dst 0x0006 net_idx=0 elem 0x0007 app_idx=0 model_id=0x1000
I: bt_mesh_cfg_cli_mod_sub_add dst 0x0006 net_idx=0 elem 0x0007 group 0xc001 model_id=0x1000
Configuration complete
Provisioning 0b2593257a534b4db4da6cda85ac74b2 (Mesh Light Fixture)
Waiting for node to be added...
Added node 0x0008
Configuring node 0x0008...
Configuring 0x0008...
I: bt_mesh_cfg_cli_net_key_add dst 0x0008 net_idx=0 net_key_idx=0
I: bt_mesh_cfg_cli_app_key_add dst 0x0008 net_idx=0 net_key_idx=0 app_key_idx=0
I: bt_mesh_cfg_cli_mod_app_bind dst 0x0008 net_idx=0 elem 0x0008 app_idx=0 model_id=0x1300
I: bt_mesh_cfg_cli_mod_sub_add dst 0x0008 net_idx=0 elem 0x0008 group 0xc002 model_id=0x1300
I: bt_mesh_cfg_cli_mod_app_bind dst 0x0008 net_idx=0 elem 0x0009 app_idx=0 model_id=0x130f
I: bt_mesh_cfg_cli_mod_app_bind dst 0x0008 net_idx=0 elem 0x0009 app_idx=0 model_id=0x1000
I: bt_mesh_cfg_cli_mod_sub_add dst 0x0008 net_idx=0 elem 0x0009 group 0xc002 model_id=0x1000
Configuration complete
```

## How scenario rows select a recipe

Scenario matching (UUID longest-prefix first, then exact device name) lives in `src/mesh_net_cfg.c`
(`scenario_find_row()`).

- `mesh_scenario_lookup_recipe_id()` reads the current recipe for a UUID/name pair without
  advancing `NODE_NAME_ROTATE` state. Auto mode uses this to decide whether to provision and which
  `recipe_id` to pass into the provisioner thread.
- After `node_added`, `configure_node()` either calls `mesh_config_apply_recipe()` with the latched
  recipe (auto or explicit path) or `mesh_config_apply()` with the cached GAP name (manual
  `prov provision` without a preset recipe).
- For `NODE_NAME_ROTATE`, `recipe_idx` advances only after the selected recipe’s configuration
  steps finish successfully (`mesh_config_apply()` or the explicit recipe path in
  `configure_node()`). Failed provisioning or failed configuration does not consume the next slot,
  so the next device with the same name still gets the same rotating choice.

1. **UUID prefix matching** (first if any `NODE_PREFIX` / `NODE_FULL` rows exist):
  - Rows match by the longest prefix of the device UUID.
  - Example: `NODE_PREFIX("A001", RECIPE_X)` matches UUID "A001...".
2. **Device name matching:**
  - Uses the GAP advertised name from active scanning.
  - `NODE_NAME("My Light", RECIPE_X)` matches devices advertising "My Light".
  - `NODE_NAME_ROTATE("My Light", RECIPE_A, RECIPE_B)` cycles recipes for successive devices with
    the same name after each successful configuration, not after failed attempts.

Group addresses:

- `GRP_1` = `0xC001`
- `GRP_2` = `0xC002`

(Defined in `include/mesh_recipes.h`.)

Unicast addresses (provisioner and provisionees):

- This provisioner uses primary element unicast 0x0001 (`self_addr` in
  `src/provisioner_handler.c`).
- Provisionees get contiguous unicast addresses from the Mesh CDB. Provisioning uses automatic
  addressing (`bt_mesh_provision_adv` with `addr == 0`).
- On a fresh network with only this provisioner, the first device is usually assigned from
  0x0002.
  Each node uses `num_elem` consecutive addresses (for example two elements: 0x0002 – 0x0003).
  Further nodes use the next free ranges.
- Erase the provisioner’s flash or mesh settings to reset allocation for a new deployment.

---

## Adding or changing recipes

A **recipe** lists bind / publish / subscribe steps for a node type. A **scenario row** in
`src/scenario_network1.c` only maps a device (advertised name or UUID prefix) to a recipe ID. Put
mesh configuration in the recipe; put deployment matching in the scenario file.

To add a new recipe, touch these in order:

1. `include/mesh_recipes.h`
   - Add deployment constants if needed (for example a new `GRP_*` next to `GRP_1` / `GRP_2`).
   - Extend `enum mesh_recipe_id` with a new `RECIPE_*` name before `RECIPE_COUNT`, and assign an
     explicit numeric value if you want stable IDs across releases or tooling. Do not reuse a
     numeric ID for a different meaning once devices or scripts depend on it.

2. `src/mesh_recipes.c`
   - Define a `static const struct mesh_setup_step ...[]` array for the node type. Start with
     `NETKEY_ADD` / `APPKEY_ADD`, then `BIND`, `SUB`, and/or `PUB` as required. Model references
     use `SIG_MODEL(BT_MESH_MODEL_ID_...)` (see `include/mesh_config_types.h` for `BIND`, `SUB`,
     `PUB`, `BIND_ALL`, `SUB_ALL`, `PUB_ALL`).
   - Register the array in `mesh_recipes[]` with `RECIPE_ENTRY(your_steps)`, indexed by the new
     enum value. Keep the `[RECIPE_NONE]` and `[RECIPE_COUNT]` layout consistent so every ID has
     one entry.

3. `src/mesh_net_cfg.c`
   - Add a string for `mesh_recipe_name()` in the `recipe_names[]` table at the same index as your
     enum value. Logs and shell output use this when printing the recipe.

4. `src/scenario_network1.c` (or another scenario source you compile in)
   - Reference the new `RECIPE_*` in `NODE_NAME`, `NODE_NAME_ROTATE`, `NODE_PREFIX`, or
     `NODE_FULL` as needed. If you add a new scenario table or file, add it to `CMakeLists.txt` and
     point `src/node_setup.c` / `src/prov_shell.c` at the scenario array you intend to use (today
     they use `scenario_b1_corridor` from `scenario_network1.h`).

Changing an existing recipe is usually limited to the step array in `mesh_recipes.c` (and possibly
constants in `mesh_recipes.h`). Renumbering enum values affects anything that stores or displays
recipe IDs; prefer additive IDs when extending the network.

---

## Network 1: building corridor and EnOcean switches

### Scenario overview

Network 1 is a corridor lighting setup with EnOcean switches and three node types:


| Node type            | Recipe                                            | Behavior |
| -------------------- | ------------------------------------------------- | -------- |
| Mesh Light Switch    | Rotates RECIPE_LIGHT_SW_1 / RECIPE_LIGHT_SW_2 | Two-element switch; element 0 publishes on/off. Recipe 1 targets GRP_1, recipe 2 targets GRP_2. Element 1 is app-bound but does not publish. |
| Mesh Light Fixture   | Rotates RECIPE_LIGHT_CTRL_1 / RECIPE_LIGHT_CTRL_2 | Two-element controller; subscribes to light level and on/off. Rotates GRP_1 / GRP_2 recipes per device. |
| Mesh Silvair EnOcean | RECIPE_ENOCEAN_BCAST                              | Two-element broadcast node; publishes level and on/off to both groups (suited to four-button EnOcean switches). |


Matching:

The scenario `scenario_b1_corridor` uses name-based matching:

```c
NODE_NAME_ROTATE("Mesh Light Switch", RECIPE_LIGHT_SW_1, RECIPE_LIGHT_SW_2)
NODE_NAME_ROTATE("Mesh Light Fixture", RECIPE_LIGHT_CTRL_1, RECIPE_LIGHT_CTRL_2)
NODE_NAME("Mesh Silvair EnOcean", RECIPE_ENOCEAN_BCAST)
```

UUID-based matching (optional):

Commented `NODE_FULL` and `NODE_PREFIX` lines in `scenario_network1.c` show how to add UUID
matching for fixed devices or prefixes. Uncomment and adjust as needed; keep prefix rules
unambiguous if you use several prefix rows.

---

### Publication and subscription

All recipes share one application key (`APPKEY_IDX` = index 0).

#### Light controller subscriptions

Subscribes to light level and on/off messages.


| Recipe | Element | Model                           | Subscribe to |
| ------ | ------- | ------------------------------- | ------------ |
| 1      | 0       | Light Lightness Server (0x1300) | GRP_1      |
| 1      | 1       | Generic OnOff Server (0x1000)   | GRP_1      |
| 2      | 0       | Light Lightness Server (0x1300) | GRP_2      |
| 2      | 1       | Generic OnOff Server (0x1000)   | GRP_2      |


Note: Light LC Server (`0x130F`) on element 1 is app-bound but has no subscription.

#### Light switch publications

Two-element switch: element 0 publishes on/off; element 1 is app-bound but does not publish.


| Recipe | Element | Model                         | Publishes to |
| ------ | ------- | ----------------------------- | ------------ |
| 1      | 0       | Generic OnOff Client (0x1001) | GRP_1      |
| 2      | 0       | Generic OnOff Client (0x1001) | GRP_2      |


#### Dimmer publications

Single-element light level client. Defined in `mesh_recipes.c` (RECIPE_DIMMER_1,
RECIPE_DIMMER_2) but not used in the current Network 1 scenario; keep it for deployments that
include a standalone dimmer.


| Recipe | Model                           | Publishes to |
| ------ | ------------------------------- | ------------ |
| 1      | Light Lightness Client (0x1302) | GRP_1      |
| 2      | Light Lightness Client (0x1302) | GRP_2      |


#### EnOcean broadcast publications

Two-element broadcaster with level and on/off clients.


| Element | Models                                                       | Publishes to | Notes |
| ------- | ------------------------------------------------------------ | ------------ | ----- |
| 0       | Generic Level Client (0x1003), Generic OnOff Client (0x1001) | GRP_1      | Both models publish to the same group. |
| 1       | Generic Level Client (0x1003), Generic OnOff Client (0x1001) | GRP_2      | Same as element 0, other group. |


#### Interaction summary

- Publishers (those sending messages): switches, dimmers, and EnOcean broadcast nodes publish to GRP_1 /
  GRP_2.
- Subscribers (those receiving messages): light fixtures subscribe on Light Lightness Server and Generic OnOff
  Server.
- Switch recipe mapping: recipe 1 element 0 => GRP_1, recipe 2 element 0 => GRP_2. Devices with
  the same name alternate recipes, so two adjacent switches can cover both groups independently.

---

## Notes

- Provisioner storage: with `CONFIG_SETTINGS`, the CDB and keys survive reboot. Erase the
  provisioner’s flash for a clean network, or use a dedicated provisioner image per deployment.
- Group addresses: GRP_1 and GRP_2 are group destinations for pub/sub, not unicast addresses.
- Build location: build from `mesh_samples/provisioner_network_setup` in your tree, not from an
  unrelated upstream Zephyr path.
