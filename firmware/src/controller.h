#ifndef __CONTROLLER__
#define __CONTROLLER__
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

/** Controller Channels (i.e. 4 Lights or Scenes) */

#define CHANNEL_0 0
#define CHANNEL_1 1
#define CHANNEL_2 2
#define CHANNEL_3 3
#define CHANNEL_COUNT 4  // I only have 2 lights

/** Controller Operations (as transmitted by the remote switch) */
#define OP_ON 0
#define OP_OFF 1
#define OP_DIM_UP 2
#define OP_DIM_DOWN 3
#define OP_COUNT 4

#define STATE_OFF 0
#define STATE_ON 1

/** Number of button presses to go from full brightness to off */
#define DIM_LEVELS 64

#define ROWS_PER_CHANNEL 6
/**
 * Each row has a Pulse Sequence Length, followed by the sequence
 * of 1s that get transmitted before another 0 gets transmitted.
 *
 * Lists all the Controller Operations for each channel beginning
 * at OP_ON for CHANNEL_0
 */
// clang-format off
#define PULSESEQ {4, 4, 3, 1, 2, 0, 0, \
                  2, 4, 6, 0, 0, 0, 0, \
                  4, 4, 1, 1, 0, 0, 0, \
                  6, 5, 1, 1, 1, 1, 1, \
                  4, 4, 3, 2, 1, 0, 0, \
                  6, 5, 1, 1, 1, 1, 1, \
                  4, 3, 4, 1, 2, 0, 0, \
                  2, 3, 7, 0, 0, 0, 0, \
                  3, 3, 5, 1, 1, 0, 0, \
                  6, 5, 1, 1, 1, 1, 1, \
                  4, 3, 4, 2, 1, 0, 0, \
                  6, 5, 1, 1, 1, 1, 1} 
 // clang-format on    

/** Size of a row above */
#define PLUSESEQ_SIZE 7

/* Number of byes in the actual message transmitted by the radio*/
#define TRANSMIT_BUF_SIZE 11

struct light_state {
  int state;
  int brightness;
};

typedef struct controller {
  struct zbus_channel *state_update_channel;
  char* device_id;
  uint8_t num_lights;
  struct light_state state[CHANNEL_COUNT];
} controller_t;

struct state_update {
  int channel;
  int state;
  int brightness;
};

typedef struct f_sum {
  sys_snode_t snode;
  struct state_update su;
} f_sum_t;

#ifdef __cplusplus
extern "C" {
#endif

int request_state(struct controller *controller, int channel, int state,
                  bool set_brightness, int brightness);

#ifdef __cplusplus
}
#endif

#endif /* __CONTROLLER__ */