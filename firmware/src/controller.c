#include "controller.h"

#include <stdint.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "radio.h"

LOG_MODULE_DECLARE(gs, CONFIG_GETSMART_LOG_LEVEL);

static void code_to_tx_payload(uint8_t *seq, uint8_t len, uint8_t *msg) {
  // 010101
  // 000 010101010101010101 (9)
  // 0 0101010101010101010101 (11)

  LOG_INF("Code to payload, %d", len);
  LOG_HEXDUMP_INF(seq, len, "Seq: ");

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
      /// LOG_INF("Bit %d, Code: 0x%02x", bit, code);

      bit -= 2;
    }
    bit -= 1;
  }
  // code |= 1 << (bit - 2);
  LOG_INF("Ending at Bit %d, Code: 0x%08x", bit, code);

  msg[6] = code >> 24;
  msg[7] = code >> 16;
  msg[8] = code >> 8;
  msg[9] = code;
  // memcpy(&msg[6], &code, sizeof(code));
  msg[10] = 0x00;
}

static void update_state(struct controller *controller, int channel, int state,
                         int brightness) {
  controller->state[channel].state = state,
  controller->state[channel].brightness = brightness;

  struct state_update update = {channel, state, brightness};
  int res =
      zbus_chan_pub(controller->state_update_channel, &(update), K_FOREVER);
  LOG_INF("Updated State: channel %d, status:%d, brightness %d, Published: %d",
          channel, state, brightness, res);
}

static int ctlr_on(struct controller *controller, int channel) {
  LOG_INF("Radio Send: Channel %d, Code: ON", channel);
  uint8_t msg[11];
  uint8_t len = 0;
  uint8_t seq[10];

  if (channel == 0) {
    len = 4;
    uint8_t seq2[] = {4, 3, 1, 2};
    memccpy(&seq, &seq2, 0, 4);
  } else if (channel == 1) {
    len = 4;
    uint8_t seq2[] = {3, 4, 1, 2};
    memccpy(&seq, &seq2, 0, len);
  } else {
    return -EINVAL;
  }
  code_to_tx_payload(seq, len, msg);
  radio_tx_repeat(msg, 11, 4);
  update_state(controller, channel, STATE_ON, DIM_LEVELS);
  return 0;
}

static int ctlr_off(struct controller *controller, int channel) {
  LOG_INF("Radio Send: Channel %d, Code: OFF", channel);
  uint8_t msg[11];
  uint8_t seq[10];
  uint8_t len;
  if (channel == 0) {
    len = 2;
    uint8_t seq2[] = {4, 6};
    memccpy(&seq, &seq2, 0, 2);

  } else if (channel == 1) {
    len = 2;
    uint8_t seq2[] = {3, 7};
    memccpy(&seq, &seq2, 0, len);
  } else {
    return -EINVAL;
  }
  code_to_tx_payload(seq, len, msg);
  for (int i = 0; i < 4; i++) {
    radio_tx(msg, 11);
  }

  update_state(controller, channel, STATE_OFF, 0);
  return 0;
}

static int ctlr_dim_up(struct controller *controller, int channel, int steps) {
  LOG_INF("Radio Send: Channel %d, Dim Up: %d", channel, steps);
  uint8_t msg[11] = {0};
  uint8_t msg1[11] = {0};
  uint8_t seq[10];
  uint8_t len;
  if (channel == 0) {
    uint8_t len = 4;
    uint8_t seq[] = {4, 4, 1, 1};
    code_to_tx_payload(seq, len, msg);
    len = 6;
    uint8_t seq1[] = {5, 1, 1, 1, 1, 1};
    code_to_tx_payload(seq1, len, msg1);
    for (int i = 0; i < steps; i++) {
      radio_tx_repeat(msg, 11, 1);
      radio_tx_repeat(msg1, 11, 1);
    }
  } else if (channel == 1) {
    uint8_t len = 4;
    uint8_t seq[] = {3, 5, 1, 1};
    code_to_tx_payload(seq, len, msg);
    len = 6;
    uint8_t seq1[] = {5, 1, 1, 1, 1, 1};
    code_to_tx_payload(seq1, len, msg1);
  } else {
    return -EINVAL;
  }
  for (int i = 0; i < steps; i++) {
    radio_tx_repeat(msg, 11, 1);
    radio_tx_repeat(msg1, 11, 1);
    k_sleep(K_MSEC(1));
  }
  update_state(controller, channel, STATE_ON,
               controller->state[channel].brightness + steps);
  return 0;
}

static int ctlr_dim_down(struct controller *controller, int channel,
                         int steps) {
  LOG_INF("Radio Send: Channel %d, Dim Down: %d", channel, steps);
  uint8_t msg[11] = {0};
  uint8_t msg1[11] = {0};

  uint8_t len;
  if (channel == 0) {
    uint8_t len = 4;
    uint8_t seq[] = {4, 3, 2, 1};
    code_to_tx_payload(seq, len, msg);
    len = 6;
    uint8_t seq1[] = {5, 1, 1, 1, 1, 1};
    code_to_tx_payload(seq1, len, msg1);
    for (int i = 0; i < steps; i++) {
      radio_tx_repeat(msg, 11, 1);
      radio_tx_repeat(msg1, 11, 1);
    }
  } else if (channel == 1) {
    uint8_t len = 4;
    uint8_t seq[] = {3, 4, 2, 1};
    code_to_tx_payload(seq, len, msg);
    len = 6;
    uint8_t seq1[] = {5, 1, 1, 1, 1, 1};
    code_to_tx_payload(seq1, len, msg1);
  } else {
    return -EINVAL;
  }
  for (int i = 0; i < steps; i++) {
    radio_tx_repeat(msg, 11, 1);
    radio_tx_repeat(msg1, 11, 1);
    k_sleep(K_MSEC(1));
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
