#ifndef __CONTROLLER__
#define __CONTROLLER__
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "radio.h"

#define CHANNEL_ALL 0
#define CHANNEL_1 1
#define CHANNEL_2 2
#define CHANNEL_3 3
#define CHANNEL_4 4
#define CHANNEL_COUNT 5

#define OP_ON 0
#define OP_OFF 1
#define OP_DIM_UP 2
#define OP_DIM_DOWN 3
#define OP_COUNT 4

#define STATE_OFF 0
#define STATE_ON 1

#define DIM_LEVELS 64  // Starts at 0?

#define MAX_CODES 4
#define CODE_INDEX(ch, op) \
  (ch * ((CHANNEL_COUNT - 1) * (MAX_CODES + 1))) + (op * (MAX_CODES + 1))

// clang-format off
// const uint8_t codes [12 * 5] = 
//                        { 3, 1, 2, 3, 0,  // ALL ON
//                          1, 1, 0, 0, 0,  // ALL OFF
//                          2, 1, 2, 0, 0,  // ALL DIM_UP
//                          2, 1, 2, 0, 0,  // ALL DIM_DOWN
//                          2, 1, 2, 0, 0,  // CH1 ON
//                          2, 1, 2, 0, 0 };// CH1 OFF
// clang-format off                        

struct light_state {
  int state;
  int brightness;
};

typedef struct controller {
  struct k_fifo *fifo;
  char* device_id;
  radio_t radio;
  uint8_t num_lights;
  struct light_state state[4];
} controller_t;

typedef struct state_update {
  int channel;
  int state;
  int brightness;
} su_t;

typedef struct f_sum {
  sys_snode_t snode;
  struct state_update su;
} f_sum_t;

int request_state(struct controller *controller, int channel, int state,
                  bool set_brightness, int brightness);

#endif /* __CONTROLLER__ */