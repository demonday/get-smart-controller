#include <stdlib.h>
#include <zephyr/logging/log.h>

#include "../lib/radiolib/src/RadioLib.h"
#include "../lib/radiolib/zephyr/src/ZephyrHal.h"
#include "../lib/radiolib/zephyr/src/ZephyrModule.h"

LOG_MODULE_REGISTER(gs_radio, CONFIG_GETSMART_LOG_LEVEL);

#define DEFAULT_RADIO_NODE DT_ALIAS(radio0)
BUILD_ASSERT(DT_NODE_HAS_STATUS(DEFAULT_RADIO_NODE, okay),
             "No default sx1278 radio specified in DT");

SX1278* fsk;

extern "C" {
int radio_init() {
  const struct device* radio_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
  LOG_INF("[%s] Checking SPI Ready...", radio_dev->name);
  if (!device_is_ready(radio_dev)) {
    LOG_ERR("[%s] Device Not Ready.", radio_dev->name);
    return -1;
  }
  LOG_INF("[%s] Device Ready.", radio_dev->name);

  ZephyrHal* hal = new ZephyrHal(radio_dev);
  LOG_INF("Hal Ready.");

  // TODO: How can I init the module with the radio device Params
  // fsk = new SX1278(new Module(hal, RADIOLIB_NC, 47, 21, 48));
  fsk = new SX1278(new ZephyrModule(hal));

  LOG_INF("Initializing FSK ...");
  int state = fsk->beginFSK(433.978, 1.6, 30.0, 7.8, 8, 0, false);
  if (state != RADIOLIB_ERR_NONE) {
    LOG_ERR("beginFSK failed, code %d", state);
    return (1);
  }
  LOG_INF("beginFSK success!");
  fsk->setGain(1);
  return 0;
}

int radio_tx_repeat(uint8_t* msg, uint8_t len, uint8_t count) {
  for (int i = 0; i < count; i++) {
    LOG_HEXDUMP_INF(msg, len, "TX:");
    fsk->transmit(msg, len);
  }
  return 0;
}
int radio_tx(uint8_t* msg, uint8_t len) { return radio_tx_repeat(msg, len, 1); }
}