
#include "radio.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gs_r, CONFIG_GETSMARTRADIO_LOG_LEVEL);

int radio_init(radio_cfg_t *cfg) { return 0; }

int radio_tx(radio_t *radio, uint8_t *msg, uint8_t len) {
  LOG_INF("radio pointer: %p", (void *)radio);
  // LOG_HEXDUMP_INF((uint8_t *)msg, len, "Transmitting:");

  return 0;
}