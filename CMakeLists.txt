#-------------------------------------------------------------------------------
# Zephyr Example Application
#
# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

zephyr_compile_options(-DCONFIG_ESP_SYSTEM_GDBSTUB_RUNTIME)
target_sources(app PRIVATE src/main.c src/mqtt_thread.c src/radio.c src/controller.c src/config_mgr.c)
