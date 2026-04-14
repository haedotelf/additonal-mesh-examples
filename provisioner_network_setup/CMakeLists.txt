#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)
set(QEMU_EXTRA_FLAGS -s)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bluetooth_mesh_provisioner)

target_sources(app PRIVATE
  src/main.c
  src/prov_cache.c
  src/provisioner_handler.c
  src/prov_scan_names.c
  src/prov_shell.c
  src/mesh_net_cfg.c
  src/mesh_recipes.c
  src/scenario_network1.c
)
target_include_directories(app PRIVATE include)

if (CONFIG_BUILD_WITH_TFM)
  target_include_directories(app PRIVATE
    $<TARGET_PROPERTY:tfm,TFM_BINARY_DIR>/api_ns/interface/include
  )
endif()
