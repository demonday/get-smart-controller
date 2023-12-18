#include "controller.h"

int mqtt_thread_init(controller_t *ctrl, struct mqtt_client *cl);
int publish_state_update(struct mqtt_client *client, struct state_update *su);