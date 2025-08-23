#include <app_version.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/watchdog.h>

#include "config_mgr.h"
#include "controller.h"
#include "mqtt_thread.h"
#include "radio.h"
#include "wifi.h"

controller_t *controller;

ZBUS_CHAN_DEFINE(chan_state_updates,   /* Name */
                 struct state_update,  /* Message type */
                 NULL,                 /* Validator */
                 NULL,                 /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT(.channel = -1, .state = -1, .brightness = -1));

/* 1000 msec = 1 sec */
#define WDT_FEED_INTERVAL_MS 1000

LOG_MODULE_REGISTER(gs, CONFIG_GETSMART_LOG_LEVEL);

extern "C"
{
  int main(void)
  {
    LOG_INF("GET Smart Wireless Lighting System Controller (%s) \n",
            APP_VERSION_STRING);

    const struct device *wdt;
    int wdt_channel_id;
    struct wdt_timeout_cfg wdt_config = {
        .window = {
            .min = 0,
            .max = 10000, // 10 seconds timeout
        },
        .callback = NULL, // NULL means system reset on timeout
        .flags = WDT_FLAG_RESET_SOC,
    };

    wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
    if (!device_is_ready(wdt))
    {
      LOG_ERR("Watchdog device not ready");
      return -1;
    }

    wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
    if (wdt_channel_id < 0)
    {
      LOG_ERR("Watchdog install error");
      return -1;
    }

    if (wdt_setup(wdt, 0) < 0)
    {
      LOG_ERR("Watchdog setup error");
      return -1;
    }

    wifi_init();

    LOG_INF("Sleeping so wifi can connect");
    k_sleep(K_MSEC(5000));
    wdt_feed(wdt, wdt_channel_id);
    k_sleep(K_MSEC(5000));

    /* Setup the device config */
    /* TODO Move to board? */
    int res = cfg_init();
    if (res < 0)
    {
      LOG_INF("Failed to init configuration.");
      return res;
    }
    cfg_inc_boot();
    cfg_print();

    static char device_id[CFG_SIZE_DEVICEID_ID];
    // static char *device_id = "0f3def"

    cfg_get_value(CFG_DEVICEID_ID, &device_id, CFG_SIZE_DEVICEID_ID);

    controller = (controller_t *)malloc(sizeof(controller_t));
    controller->num_lights = 2; // TODO
    controller->device_id = device_id;
    controller->state_update_channel = (struct zbus_channel *)&chan_state_updates;

    // LOG_INF("radio pointer (main): %p", (void *)&controller->radio);

    mqtt_thread_init(controller);

    radio_init();

    while (1)
    {
      k_sleep(K_SECONDS(1));
      wdt_feed(wdt, wdt_channel_id);
    }

    return 0;
  }
}
