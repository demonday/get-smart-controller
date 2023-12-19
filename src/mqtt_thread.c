#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zbus/zbus.h>

#include "controller.h"

LOG_MODULE_DECLARE(gs, CONFIG_GETSMART_LOG_LEVEL);

#define MQTT_HA_DISCOVER_TOPIC "homeassistant/light/getsmart/%s-%d/config"
#define MQTT_STATE_TOPIC "getsmart/device/%s/channel/%d/state"
#define MQTT_COMMAND_TOPIC "getsmart/device/%s/channel/%d/cmnd"

#define MQTT_UPDATE_STATE_PAYLOAD "{\"state\":\"%s\", \"brightness\":%d}"

#define MQTT_HA_DISCOVER_PAYLOAD                                      \
  "{\"name\":\"%s\",\"command_topic\":\"%s\",\"state_topic\":\"%s\"," \
  "\"schema\":\"json\",\"brightness\":true,\"brightness_scale\":64}"

#define MQTT_STATE_ON "ON"

typedef struct msg_command {
  char *state;
  int brightness;
} msg_command_t;

static const struct json_obj_descr msg_command_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct msg_command, state, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct msg_command, brightness, JSON_TOK_NUMBER),
};

/* Thread Stack */
#define STACKSIZE 4096
#define APP_MQTT_BUFFER_SIZE 128
#define CONFIG_MQTT_PAYLOAD_BUFFER_SIZE 4256

K_THREAD_STACK_DEFINE(mqtt_thread_stack, STACKSIZE);

K_MUTEX_DEFINE(sock_lock);

/* Thread Data */
struct k_thread mqtt_thread_data;

/* MQTT Client Struct */
static struct mqtt_client client;

/* pointer to the controller */
static controller_t *controller;

/* MQTT Broker details. */
static struct sockaddr_storage broker;

/* MQTT Broker Details */
static const char *server_addr = "192.0.2.2";
static uint16_t server_port = 1883;
// static const char *username = "username";
// static const char *password = "passwd";
#define MQTT_CLIENTID "getsmart_publisher"

/* Client Identifer TODO: Based on MAC*/
static const char *client_id = "get-smart-MAC";

/* Buffers for MQTT client. */
static uint8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[APP_MQTT_BUFFER_SIZE];
static uint8_t payload_buf[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];

#define SOCKET_TIMEOUT_MS 2000

/* File descriptor for socket */
static struct pollfd fds;

/* Connection Status */
static bool connected;

int fds_init(struct mqtt_client *client, struct pollfd *fds) {
  if (client->transport.type == MQTT_TRANSPORT_NON_SECURE) {
    fds->fd = client->transport.tcp.sock;
  } else {
    return -ENOTSUP;
  }

  fds->events = POLLIN;

  return 0;
}

static void data_print(uint8_t *prefix, uint8_t *data, size_t len) {
  char buf[len + 1];

  memcpy(buf, data, len);
  buf[len] = 0;
  LOG_INF("%s%s", (char *)prefix, (char *)buf);
}

static int get_received_payload(struct mqtt_client *c, size_t length) {
  int ret;
  int err = 0;

  /* Return an error if the payload is larger than the payload buffer.
   * Note: To allow new messages, we have to read the payload before returning.
   */
  if (length > sizeof(payload_buf)) {
    err = -EMSGSIZE;
  }

  /* Truncate payload until it fits in the payload buffer. */
  while (length > sizeof(payload_buf)) {
    ret = mqtt_read_publish_payload_blocking(c, payload_buf,
                                             (length - sizeof(payload_buf)));
    if (ret == 0) {
      return -EIO;
    } else if (ret < 0) {
      return ret;
    }

    length -= ret;
  }

  ret = mqtt_readall_publish_payload(c, payload_buf, length);
  if (ret) {
    return ret;
  }

  return err;
}

static void extract_device_info(const char *topic_name, char **device_id,
                                int *channel) {
  char *token, *temp_str;
  int count = 0;

  // Ensure the input string is not NULL
  if (topic_name == NULL) {
    return;
  }

  // Duplicate the string since strtok modifies the string
  temp_str = strdup(topic_name);
  if (temp_str == NULL) {
    // Handle memory allocation failure
    return;
  }

  token = strtok(temp_str, "/");
  while (token != NULL) {
    count++;
    if (count == 3) {  // Assuming device_id is always the third token
      *device_id = strdup(token);
      if (*device_id == NULL) {
        // Handle memory allocation failure
        free(temp_str);
        return;
      }
    }
    if (count == 5) {  // Assuming the channel is always the fifth token
      *channel = atoi(token);
      break;
    }
    token = strtok(NULL, "/");
  }

  free(temp_str);  // Free the temporary string
}

static void handle_msg_command(char *topic_name, char *msg) {
  char *device_id = NULL;
  int channel = 0;

  extract_device_info(topic_name, &device_id, &channel);

  struct msg_command command;
  int ret = json_obj_parse(msg, strlen(msg), msg_command_descr,
                           sizeof(msg_command_descr), &command);

  bool set_brightness = (ret == 3);
  if (ret != 3) {
    LOG_INF("No brightness in MQTT state message");
  }

  LOG_INF("Device:%s, Channel:%d, State: %s, Brightness: %d", device_id,
          channel, command.state, command.brightness);

  int state =
      (strcmp(command.state, MQTT_STATE_ON) == 0) ? STATE_ON : STATE_OFF;
  request_state(controller, channel, state, set_brightness, command.brightness);

  if (device_id != NULL) {
    free(device_id);  // Free the device_id string
  }
}

/* Subscribe to the MQTT Topic(s) to control the device */
static int subscribe_cmnds(struct mqtt_client *const client) {
  struct mqtt_topic topic_list[4];

  for (int i = 0; i < controller->num_lights; i++) {
    char buf[256];
    sprintf(buf, MQTT_COMMAND_TOPIC, controller->device_id, i);
    char *topic_name = malloc(strlen(buf) + 1);
    strcpy(topic_name, buf);

    struct mqtt_topic subscribe_topic = {
        .topic = {.utf8 = topic_name, .size = strlen(topic_name)},
        .qos = MQTT_QOS_1_AT_LEAST_ONCE};

    topic_list[i] = subscribe_topic;
    LOG_INF("Subscribing to: %s len %u", topic_name,
            (unsigned int)strlen(topic_name));
  }

  const struct mqtt_subscription_list subscription_list = {
      .list = (struct mqtt_topic *)&topic_list,
      .list_count = controller->num_lights,
      .message_id = 1234};

  int res = mqtt_subscribe(client, &subscription_list);

  // for (int i = 0; i < controller->num_lights; i++) {
  //   free((void *)topic_list[i].topic.utf8);
  // }

  return res;
}

// static int unsubscribe_cmnds(struct mqtt_client *const client) {
//   return 0;  // TODO
// }

int publish_hadiscover() {
  // struct mqtt_publish_param param;
  // int res = 0;
  // for (int i = 0; i < controller->num_lights; i++) {
  //   char buf[512];

  //   sprintf(buf, MQTT_HA_DISCOVER_TOPIC, controller->device_id, i);
  //   char *topic_name = malloc(strlen(buf));
  //   strcpy(topic_name, buf);

  //   sprintf(buf, MQTT_COMMAND_TOPIC, controller->device_id, i);
  //   char *tn_cmnd = malloc(strlen(buf));
  //   strcpy(tn_cmnd, buf);

  //   sprintf(buf, MQTT_STATE_TOPIC, controller->device_id, i);
  //   char *tn_state = malloc(strlen(buf));
  //   strcpy(tn_state, buf);

  //   // char device_name[20];
  //   // sprintf(device_name, "%s-%s", controller->device_id, i);
  //   sprintf(buf, MQTT_HA_DISCOVER_PAYLOAD, controller->device_id, tn_cmnd,
  //           tn_state);
  //   char *payload = malloc(strlen(buf));
  //   strcpy(payload, buf);

  //   param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
  //   param.message.topic.topic.utf8 = (uint8_t *)&topic_name;
  //   param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
  //   param.message.payload.data = (uint8_t *)&payload;
  //   param.message.payload.len = strlen(param.message.payload.data);
  //   param.message_id = sys_rand32_get();
  //   param.dup_flag = 0U;
  //   param.retain_flag = 0U;

  //   k_mutex_lock(&sock_lock, K_FOREVER);
  //   LOG_INF("Publishing to %s", topic_name);
  //   res = mqtt_publish(&client, &param);
  //   k_mutex_unlock(&sock_lock);

  //   free(topic_name);
  //   free(tn_cmnd);
  //   free(tn_state);
  //   free(payload);

  //   if (res != 0) {
  //     LOG_ERR("Error - Publish HA Discover: %d", res);
  //     return res;
  //   }
  // }
  return 0;
}

int publish_state_update(struct mqtt_client *client, struct state_update *su) {
  struct mqtt_publish_param param;

  char topic_name[256];
  sprintf(topic_name, MQTT_STATE_TOPIC, controller->device_id, su->channel);

  char payload[256];
  sprintf(payload, MQTT_UPDATE_STATE_PAYLOAD,
          (su->state == STATE_ON) ? "ON" : "OFF", su->brightness);

  param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
  param.message.topic.topic.utf8 = (uint8_t *)&topic_name;
  param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
  param.message.payload.data = (uint8_t *)&payload;
  param.message.payload.len = strlen(param.message.payload.data);
  param.message_id = sys_rand32_get();
  param.dup_flag = 0U;
  param.retain_flag = 0U;

  // LOG_INF("Calling publish");
  k_mutex_lock(&sock_lock, K_FOREVER);
  // LOG_INF("Mutex - P - L");
  LOG_INF("Publishing state update to %s", topic_name);
  int res = mqtt_publish(client, &param);
  k_mutex_unlock(&sock_lock);
  // LOG_INF("Mutex - P - U");
  if (res != 0) {
    LOG_ERR("Error - Publish Result: %d", res);
  }

  return res;
}

ZBUS_SUBSCRIBER_DEFINE(state_update_subscriber, 4);

static void mqtt_subscriber_task(void) {
  const struct zbus_channel *chan;

  while (!zbus_sub_wait(&state_update_subscriber, &chan, K_FOREVER)) {
    struct state_update update;

    if (controller->state_update_channel == chan) {
      zbus_chan_read(controller->state_update_channel, &update, K_FOREVER);

      LOG_INF("From subscriber -> %d,%d,%d", update.channel, update.state,
              update.brightness);

      publish_state_update(&client, &update);
    }
  }
}

K_THREAD_DEFINE(subscriber_task_id, CONFIG_MAIN_STACK_SIZE,
                mqtt_subscriber_task, NULL, NULL, NULL, 3, 0, 0);

/* MQTT Message Handler */
void mqtt_message_handler(struct mqtt_client *const client,
                          const struct mqtt_evt *evt) {
  int err;

  switch (evt->type) {
    case MQTT_EVT_CONNACK:
      if (evt->result != 0) {
        LOG_INF("MQTT connect failed %d\n", evt->result);
      } else {
        LOG_INF("MQTT client connected!\n");
        connected = true;
        err = subscribe_cmnds(client);
        if (err) {
          LOG_INF("State TOPIC Subscription request failed %d\n", err);
        }
        // err = publish_hadiscover();
        // if (err) {
        //   LOG_INF("State TOPIC Subscription request failed %d\n", err);
        // }
      }
      break;

    case MQTT_EVT_PUBLISH:
      const struct mqtt_publish_param *p = &evt->param.publish;
      LOG_INF("Received message on TOPIC: %s, result=%d len=%d",
              p->message.topic.topic.utf8, evt->result, p->message.payload.len);

      // Extract the data of the recived message
      err = get_received_payload(client, p->message.payload.len);
      //  On successful extraction of data
      if (err >= 0) {
        data_print("Received: ", payload_buf, p->message.payload.len);
        handle_msg_command((char *)p->message.topic.topic.utf8, payload_buf);
        // On failed extraction of data - Payload buffer is smaller than the
        // recived data . Increase
      } else if (err == -EMSGSIZE) {
        LOG_ERR(
            "Received payload (%d bytes) is larger than the payload buffer "
            "size (%lu bytes).",
            p->message.payload.len, sizeof(payload_buf));
      } else {
        LOG_ERR("get_received_payload failed: %d. Don't send any acks...", err);
        break;
      }

      /* Send the appropiate QoS response */
      if (evt->param.publish.message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
        const struct mqtt_puback_param ack = {
            .message_id = evt->param.publish.message_id};
        mqtt_publish_qos1_ack(client, &ack);
      } else if (evt->param.publish.message.topic.qos ==
                 MQTT_QOS_2_EXACTLY_ONCE) {
        const struct mqtt_pubrec_param rec = {
            .message_id = evt->param.publish.message_id};
        mqtt_publish_qos2_receive(client, &rec);
      }
      break;

    case MQTT_EVT_PUBREC:
      if (evt->param.pubrec.message_id == 1234) {
        const struct mqtt_pubrel_param rel = {.message_id =
                                                  evt->param.pubrec.message_id};
        mqtt_publish_qos2_release(client, &rel);
      }
      break;

    case MQTT_EVT_PUBREL:
      if (evt->param.pubrel.message_id == 1234) {
        const struct mqtt_pubcomp_param comp = {
            .message_id = evt->param.pubrel.message_id};
        err = mqtt_publish_qos2_complete(client, &comp);
        if (err) {
          LOG_INF("Failed to send PUBCOMP, error: %d\n", err);
        }
      }
      break;

    case MQTT_EVT_PUBCOMP:
      if (evt->param.pubcomp.message_id == 1234) {
        LOG_INF("QoS 2 message flow completed!\n");
      }
      break;

    case MQTT_EVT_SUBACK:
      if (evt->result != 0) {
        LOG_ERR("MQTT SUBACK error %d", evt->result);
        break;
      }
      LOG_INF("SUBACK packet id: %u", evt->param.suback.message_id);
      break;

    case MQTT_EVT_DISCONNECT:
      LOG_INF("MQTT client disconnected %d", evt->result);
      connected = false;
      break;

    case MQTT_EVT_PINGRESP:
      LOG_INF("PINGRESP packet");
      break;

    default:
      LOG_INF("Unhandled MQTT event %d\n", evt->type);
      break;
  }
}

/* MQTT Thread Function */
void mqtt_thread(void *arg1, void *arg2, void *arg3) {
  struct mqtt_client *client = arg1;
  // controller_t *controller = arg2;
  int err;
  LOG_INF("Starting thread...");
  /* Broker Details */
  struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;
  broker4->sin_family = AF_INET;
  broker4->sin_port = htons(server_port);
  zsock_inet_pton(AF_INET, server_addr, &broker4->sin_addr);

  LOG_INF("Broker Config set");

  /* MQTT client configuration */
  client->broker = &broker;
  LOG_INF("Broker Configured in client...");

  client->client_id.utf8 = (uint8_t *)client_id;
  client->client_id.size = strlen(client_id);
  LOG_INF("ClientId configured...");

  //   client->user_name->utf8 = (uint8_t *)username;
  //   client->user_name->size = strlen(username);
  //   client->password->utf8 = (uint8_t *)password;
  //   client->password->size = strlen(password);
  //   client->client_id.utf8 = (uint8_t *)MQTT_CLIENTID;
  //   client->client_id.size = strlen(MQTT_CLIENTID);
  //   client->password->utf8 = (uint8_t *)MQTT_PASSWORD;
  //   client->password->size = strlen(MQTT_PASSWORD);
  //   client->user_name->utf8 = (uint8_t *)MQTT_USERNAME;
  //   client->user_name->size = strlen(MQTT_USERNAME);
  client->protocol_version = MQTT_VERSION_3_1_1;
  client->transport.type = MQTT_TRANSPORT_NON_SECURE;

  LOG_INF("Client stings configured...");

  /* MQTT buffers configuration */
  client->rx_buf = rx_buffer;
  LOG_INF("rxbuffer.");
  client->rx_buf_size = sizeof(rx_buffer);
  client->tx_buf = tx_buffer;
  LOG_INF("txbuffer.");
  client->tx_buf_size = sizeof(tx_buffer);

  LOG_INF("Attempting to connect...");

  /* Connect to MQTT broker */
  err = mqtt_connect(client);
  if (err) {
    LOG_INF("Unable to connect to MQTT broker\n");
    return;
  }
  LOG_INF("Connected...");

  err = fds_init(client, &fds);
  if (err) {
    LOG_ERR("Error in fds_init: %d", err);
    return;
  }

  /* Polling and event loop */
  while (1) {
    k_mutex_lock(&sock_lock, K_FOREVER);
    // LOG_INF("Mutex - L - L");
    err = poll(&fds, 1, 500);
    // LOG_INF("Poll Done: %d", 500);
    if (err < 0) {
      LOG_ERR("Error in poll(): %d", errno);
      k_mutex_unlock(&sock_lock);
      break;
    }

    err = mqtt_live(client);
    if ((err != 0) && (err != -EAGAIN)) {
      LOG_ERR("Error in mqtt_live: %d", err);
      k_mutex_unlock(&sock_lock);
      break;
    }

    if ((fds.revents & POLLIN) == POLLIN) {
      LOG_INF("Input");
      err = mqtt_input(client);
      if (err != 0) {
        LOG_ERR("Error in mqtt_input: %d", err);
        break;
        k_mutex_unlock(&sock_lock);
      }
    }

    if ((fds.revents & POLLERR) == POLLERR) {
      LOG_ERR("POLLERR");
      k_mutex_unlock(&sock_lock);
      break;
    }

    if ((fds.revents & POLLNVAL) == POLLNVAL) {
      LOG_ERR("POLLNVAL");
      k_mutex_unlock(&sock_lock);
      break;
    }
    k_mutex_unlock(&sock_lock);
    // LOG_INF("Mutex - L - U");

    if (!connected) {
      mqtt_abort(client);
    }
  }
}

int mqtt_thread_init(controller_t *ctrl) {
  controller = ctrl;
  mqtt_client_init(&client);
  /* Initialize MQTT client */
  k_mutex_init(&sock_lock);
  client.evt_cb = mqtt_message_handler;
  zbus_chan_add_obs(controller->state_update_channel, &state_update_subscriber,
                    K_MSEC(200));

  k_thread_create(&mqtt_thread_data, mqtt_thread_stack,
                  K_THREAD_STACK_SIZEOF(mqtt_thread_stack), mqtt_thread,
                  &client, NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);

  return 0;
}
