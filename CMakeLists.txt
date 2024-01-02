#-------------------------------------------------------------------------------
# Zephyr Example Application
#
# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app)

add_compile_definitions(RADIOLIB_EXCLUDE_CC1101 RADIOLIB_EXCLUDE_SX126X RADIOLIB_EXCLUDE_RF69 RADIOLIB_EXCLUDE_RFM2X RADIOLIB_EXCLUDE_SX1233)
add_compile_definitions(RADIOLIB_EXCLUDE_SX1231 RADIOLIB_EXCLUDE_SX128X RADIOLIB_EXCLUDE_SI443X RADIOLIB_EXCLUDE_NRF24)
add_compile_definitions(RADIOLIB_EXCLUDE_FSK4)

file(GLOB_RECURSE radiolib_sources
  "./lib/radiolib/src/*.cpp"
)

file(GLOB_RECURSE radiolib_zephyr_sources
  "./lib/radiolib/zephyr/src/*.cpp" 
)

file(GLOB_RECURSE zephyr_radio_driver_sources
  "./lib/radiolib/zephyr/src/drivers/radio/*.c" 
)

#add_subdirectory(./lib/radiolib)
#zephyr_compile_options(-DCONFIG_ESP_SYSTEM_GDBSTUB_RUNTIME)
target_sources(app PRIVATE src/main.cpp src/wifi.c src/config_mgr.c src/controller.c src/mqtt_thread.c ${radiolib_sources} ${radiolib_zephyr_sources} ${zephyr_radio_driver_sources})
