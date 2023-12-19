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

controller_t *controller;

ZBUS_CHAN_DEFINE(chan_state_updates,   /* Name */
                 struct state_update,  /* Message type */
                 NULL,                 /* Validator */
                 NULL,                 /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT(.channel = -1, .state = -1, .brightness = -1));

/* 1000 msec = 1 sec */
#define SLEEP_TIME 5000

LOG_MODULE_REGISTER(gs, CONFIG_GETSMART_LOG_LEVEL);

int main(void) {
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

  controller = malloc(sizeof(controller_t));
  controller->num_lights = 2;  // TODO
  controller->device_id = device_id;
  controller->state_update_channel = (struct zbus_channel *)&chan_state_updates;

  // LOG_INF("radio pointer (main): %p", (void *)&controller->radio);

  mqtt_thread_init(controller);

  while (1) {
    k_sleep(K_SECONDS(5));
  }

  return 0;
}
