/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app_version.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/zbus/zbus.h>

#include "config_mgr.h"
#include "controller.h"
#include "mqtt_thread.h"
#include "wifi.h"

// include the library
#include "../lib/radiolib/src/RadioLib.h"

// include the hardware abstraction layer
#include "../lib/radiolib/zephyr/src/ZephyrHal.h"

#define DEFAULT_RADIO_NODE DT_ALIAS(radio0)
BUILD_ASSERT(DT_NODE_HAS_STATUS(DEFAULT_RADIO_NODE, okay),
             "No default sx1278 radio specified in DT");

controller_t* controller;

ZBUS_CHAN_DEFINE(chan_state_updates,   /* Name */
                 struct state_update,  /* Message type */
                 NULL,                 /* Validator */
                 NULL,                 /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT(.channel = -1, .state = -1, .brightness = -1));

/* 1000 msec = 1 sec */
#define SLEEP_TIME 5000

LOG_MODULE_REGISTER(gs, CONFIG_GETSMART_LOG_LEVEL);

extern "C" {
int main(void) {
  const struct device* radio_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
  LOG_INF("[%s] Checking SPI Ready...", radio_dev->name);
  if (!device_is_ready(radio_dev)) {
    return 0;
    LOG_ERR("[%s] Device Not Ready.", radio_dev->name);
  }
  LOG_INF("[%s] Device Ready.", radio_dev->name);

  wifi_init();
  wifi_sta_connect();

  LOG_INF("Sleeping so wifi can connect");
  k_sleep(K_MSEC(10000));

  ZephyrHal* hal = new ZephyrHal(radio_dev);
  SX1278 fsk = new Module(hal, RADIOLIB_NC, 47, 21, 48);

  LOG_INF("GET Smart Wireless Lighting System Controller (%s) \n",
          APP_VERSION_STRING);

  /* Setup the device config */
  /* TODO Move to board? */
  int res = cfg_init();
  if (res < 0) {
    LOG_INF("Failed to init configuration.");
    return res;
  }
  cfg_inc_boot();
  cfg_print();

  char device_id[CFG_SIZE_DEVICEID_ID];
  cfg_get_value(CFG_DEVICEID_ID, &device_id, CFG_SIZE_DEVICEID_ID);

  controller = (controller_t*)malloc(sizeof(controller_t));
  // controller->module = &fsk;
  controller->num_lights = 1;  // TODO
  controller->device_id = device_id;
  controller->state_update_channel = (struct zbus_channel*)&chan_state_updates;

  // LOG_INF("radio pointer (main): %p", (void *)&controller->radio);

  mqtt_thread_init(controller);

  while (1) {
    k_sleep(K_SECONDS(5));
  }

  return 0;
}
}
