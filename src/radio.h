#include <stdint.h>

#ifndef __RADIO__
typedef struct radio {
  int f;
} radio_t;

typedef struct radio_cfg {
  float freq;
} radio_cfg_t;
#define __RADIO__
#endif

int radio_init(radio_cfg_t *cfg);
int radio_tx(radio_t *radio, uint8_t *msg, uint8_t len);