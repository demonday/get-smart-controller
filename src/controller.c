#include "controller.h"

#include <stdint.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "radio.h"

LOG_MODULE_DECLARE(gs, CONFIG_GETSMART_LOG_LEVEL);

static void code_to_tx_payload(uint8_t *seq, uint8_t len, uint8_t *msg) {
  // 010101
  // 000 010101010101010101 (9)
  // 0 0101010101010101010101 (11)

  msg[0] = 0x54;
  msg[1] = 0x2A;
  msg[2] = 0xAA;
  msg[3] = 0xA5;
  msg[4] = 0x55;
  msg[5] = 0x55;

  uint32_t code = 0x40000000;
  uint8_t bit = 27;
  for (int i = 0; i < len; i++) {
    for (int j = seq[i]; j > 0; j--) {
      code |= 1 << bit;
      bit -= 2;
    }
    bit -= 1;
  }
  code |= 1 << (bit - 2);

  msg[6] = code >> 24;
  msg[7] = code >> 16;
  msg[8] = code >> 8;
  msg[9] = code;
  // memcpy(&msg[6], &code, sizeof(code));
}

static void update_state(struct controller *controller, int channel, int state,
                         int brightness) {
  controller->state[channel].state = state,
  controller->state[channel].brightness = brightness;

  // TODO Better names
  f_sum_t *f_sum = malloc(sizeof(f_sum_t));
  f_sum->su.channel = channel;
  f_sum->su.state = state;
  f_sum->su.brightness = brightness;
  LOG_INF("Fifo Address:%p", controller->fifo);
  k_fifo_put(controller->fifo, f_sum);
  LOG_INF("Updated State: status:%d, brightness %d", state, brightness);
}

static int ctlr_on(struct controller *controller, int channel) {
  LOG_INF("Radio Send: Channel %d, Code: ON", channel);
  uint8_t msg[10] = {0};
  uint8_t seq[4] = {7, 1, 1, 1};
  code_to_tx_payload(seq, 4, msg);

  radio_tx(&controller->radio, msg, 10);
  update_state(controller, channel, STATE_ON, DIM_LEVELS);
  return 0;
}

static int ctlr_off(struct controller *controller, int channel) {
  LOG_INF("Radio Send: Channel %d, Code: OFF", channel);
  uint8_t msg[10] = {0};
  uint8_t seq[4] = {7, 1, 1, 1};
  code_to_tx_payload(seq, 4, msg);

  radio_tx(&controller->radio, msg, 10);

  update_state(controller, channel, STATE_OFF, 0);
  return 0;
}

static int ctlr_dim_up(struct controller *controller, int channel, int steps) {
  LOG_INF("Radio Send: Channel %d, Dim Up: %d", channel, steps);
  uint8_t msg[10] = {0};
  uint8_t seq[4] = {7, 1, 1, 1};
  code_to_tx_payload(seq, 4, msg);

  for (int i = 0; i < steps; i++) {
    radio_tx(&controller->radio, msg, 10);
  }
  return 0;
  update_state(controller, channel, STATE_ON,
               controller->state[channel].brightness + steps);
}

static int ctlr_dim_down(struct controller *controller, int channel,
                         int steps) {
  LOG_INF("Radio Send: Channel %d, Dim Down: %d", channel, steps);
  uint8_t msg[10] = {0};
  uint8_t seq[4] = {7, 1, 1, 1};
  code_to_tx_payload(seq, 4, msg);

  for (int i = 0; i < steps; i++) {
    radio_tx(&controller->radio, msg, 10);
  }
  update_state(controller, channel, STATE_ON,
               controller->state[channel].brightness - steps);
  return 0;
}

int request_state(struct controller *controller, int channel, int state,
                  bool set_brightness, int brightness) {
  LOG_INF("Current State: status:%d, brightness %d",
          controller->state[channel].state,
          controller->state[channel].brightness);
  LOG_INF("Request State: status:%d, brightness %d", state, brightness);

  if (controller->state[channel].state != state) {
    if (state == STATE_ON) {
      ctlr_on(controller, channel);
    } else {
      ctlr_off(controller, channel);
    }
  }
  if (set_brightness && controller->state[channel].brightness != brightness) {
    if (brightness <= 1) {
      ctlr_off(controller, channel);
    } else if (brightness > DIM_LEVELS) {
      LOG_ERR("Cannot exceed %d brightness levels", DIM_LEVELS);
    } else {
      int diff = brightness - controller->state[channel].brightness;
      if (diff > 0) {
        ctlr_dim_up(controller, channel, diff);
      } else {
        ctlr_dim_down(controller, channel, abs(diff));
      }
    }
  }

  return 0;
}
