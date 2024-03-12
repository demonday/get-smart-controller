#include "controller.h"

#include <stdint.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "radio.h"

LOG_MODULE_DECLARE(gs, CONFIG_GETSMART_LOG_LEVEL);

static const uint8_t pulses[] = PULSESEQ;

static uint8_t code_to_tx_payload(uint8_t *seq, uint8_t len_seq,
                                  uint8_t **msg) {
  LOG_INF("Code to payload, %d", len_seq);
  LOG_HEXDUMP_INF(seq, len_seq, "Seq: ");

  *msg = (uint8_t *)malloc(TRANSMIT_BUF_SIZE);

  (*msg)[0] = 0x54;
  (*msg)[1] = 0x2A;
  (*msg)[2] = 0xAA;
  (*msg)[3] = 0xA5;
  (*msg)[4] = 0x55;
  (*msg)[5] = 0x55;

  uint32_t code = 0x40000000;
  uint8_t bit = 27;
  for (int i = 0; i < len_seq; i++) {
    for (int j = seq[i]; j > 0; j--) {
      code |= 1 << bit;
      bit -= 2;
    }
    bit -= 1;
  }
  // code |= 1 << (bit - 2);

  (*msg)[6] = code >> 24;
  (*msg)[7] = code >> 16;
  (*msg)[8] = code >> 8;
  (*msg)[9] = code;
  (*msg)[10] = 0x00;  // TODO: Not part of the message, needs more testing
  return TRANSMIT_BUF_SIZE;
}

/**
 * Convert the Channel and OP code combo to the bytes to be transmitted
 * by the radio.
 */
static uint8_t get_radio_msg(uint8_t channel, uint8_t op, uint8_t pulse_no,
                             uint8_t **msg) {
  // Get the pattern to be transmitted
  uint8_t seq[PLUSESEQ_SIZE - 1];
  uint8_t len_seq;

  uint8_t index =
      ((channel * ROWS_PER_CHANNEL) + pulse_no + op) * PLUSESEQ_SIZE;

  // First byte of the "row" is length of the pluse sequence
  memcpy(&len_seq, &pulses, sizeof(uint8_t));
  LOG_INF("Len of seq is:%d", len_seq);

  // remainder of row is the actual sequence for this channel/op combo
  LOG_INF("Coping from index %d",
          (channel * PLUSESEQ_SIZE) + op + 1 + pulse_no);

  memcpy(&seq, pulses + index + 1, sizeof(uint8_t) * len_seq);

  // Convert it to the actual byte sequence to be transmitted
  return code_to_tx_payload((uint8_t *)&seq, len_seq, msg);
}

/**
 * Publish the state update message on the zbus
 */
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
  uint8_t *msg = NULL;
  uint8_t len = get_radio_msg(channel, OP_ON, 0, &msg);
  LOG_HEXDUMP_INF(msg, TRANSMIT_BUF_SIZE, "Msg: ");
  radio_tx_repeat(msg, len, 4);
  free(msg);
  update_state(controller, channel, STATE_ON, DIM_LEVELS);
  return 0;
}

static int ctlr_off(struct controller *controller, int channel) {
  LOG_INF("Radio Send: Channel %d, Code: OFF", channel);
  uint8_t *msg = NULL;
  uint8_t len = get_radio_msg(channel, OP_OFF, 0, &msg);
  radio_tx_repeat(msg, len, 4);
  free(msg);
  update_state(controller, channel, STATE_OFF, 0);
  return 0;
}

static int ctlr_dim_up(struct controller *controller, int channel, int steps) {
  LOG_INF("Radio Send: Channel %d, Dim Up: %d", channel, steps);
  uint8_t *msg0 = NULL;
  uint8_t *msg1 = NULL;

  uint8_t len0 = get_radio_msg(channel, OP_DIM_DOWN, 0, &msg0);
  uint8_t len1 = get_radio_msg(channel, OP_DIM_DOWN, 1, &msg1);

  for (int i = 0; i < steps; i++) {
    radio_tx(msg0, len0);
    radio_tx(msg1, len1);
    k_sleep(K_MSEC(5));
  }

  free(msg0);
  free(msg1);

  update_state(controller, channel, STATE_ON,
               controller->state[channel].brightness + steps);
  return 0;
}

static int ctlr_dim_down(struct controller *controller, int channel,
                         int steps) {
  LOG_INF("Radio Send: Channel %d, Dim Down: %d", channel, steps);
  uint8_t *msg0 = NULL;
  uint8_t *msg1 = NULL;
  uint8_t len0 = get_radio_msg(channel, OP_DIM_DOWN, 0, &msg0);
  uint8_t len1 = get_radio_msg(channel, OP_DIM_DOWN, 1, &msg1);

  radio_tx(msg0, len0);
  radio_tx(msg1, len1);

  for (int i = 0; i < steps; i++) {
    radio_tx(msg0, len0);
    radio_tx(msg1, len1);
    k_sleep(K_MSEC(5));
  }
  free(msg0);
  free(msg1);
  update_state(controller, channel, STATE_ON,
               controller->state[channel].brightness - steps);
  return 0;
}

/**
 * Transmit the required radio signals to transition the lights
 * from current state to the requested, and if successful publish
 * the new state on the zbus.
 */
int request_state(struct controller *controller, int channel, int state,
                  bool set_brightness, int brightness) {
  LOG_INF("Current State: status:%d, brightness %d",
          controller->state[channel].state,
          controller->state[channel].brightness);
  LOG_INF("Request State: status:%d, brightness %d", state, brightness);

  if (channel >= controller->num_lights) {
    LOG_ERR("Light for channel %d not configured.", channel);
    return -EINVAL;
  }

  if (controller->state[channel].state != state) {
    if (state == STATE_ON) {
      ctlr_on(controller, channel);
    } else {
      ctlr_off(controller, channel);
    }
  }
  if (set_brightness && controller->state[channel].brightness != brightness) {
    if (brightness <= 1) {
      // ctlr_off(controller, channel);
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
