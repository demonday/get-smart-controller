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

#include "config_mgr.h"
#include "controller.h"
#include "mqtt_thread.h"

controller_t *controller;

K_FIFO_DEFINE(fifo_state_updates);

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

  k_fifo_init(&fifo_state_updates);
  controller = malloc(sizeof(controller_t));
  controller->num_lights = 2;  // TODO
  controller->device_id = device_id;
  controller->fifo = &fifo_state_updates;

  LOG_INF("This Fifo Address:%p", &fifo_state_updates);
  LOG_INF("Controller Fifo Address:%p", controller->fifo);
  LOG_INF("radio pointer (main): %p", (void *)&controller->radio);

  struct mqtt_client cl;
  mqtt_thread_init(controller, &cl);
  f_sum_t *rx_data;

  while (1) {
    rx_data = k_fifo_get(&fifo_state_updates, K_FOREVER);
    LOG_INF("RX channel:%d", rx_data->su.channel);
    int res = publish_state_update(&cl, &rx_data->su);
    LOG_INF("Published Update...");
  }

  return 0;
}
