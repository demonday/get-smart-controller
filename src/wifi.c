

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

// LOG_MODULE_REGISTER("wifi", LOG_LEVEL_INF);

// TODO: make ap name dynamic based on device (e.g. MAC)
// #define SSID "getsmart_ap"
#define SSID "ALHN-A898"
#define PASSWORD "XA5nh7pZXf"

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static void handle_wifi_connect_result(struct net_mgmt_event_callback *cb) {
  const struct wifi_status *status = (const struct wifi_status *)cb->info;

  if (status->status) {
    printk("Connection request failed (%d)\n", status->status);
  } else {
    printk("Connected\n");
    // k_sem_give(&wifi_connected);
  }
}

static void handle_wifi_disconnect_result(struct net_mgmt_event_callback *cb) {
  const struct wifi_status *status = (const struct wifi_status *)cb->info;

  if (status->status) {
    printk("Disconnection request (%d)\n", status->status);
  } else {
    printk("Disconnected\n");
    // k_sem_take(&wifi_connected, K_NO_WAIT);
  }
}

static void handle_ipv4_result(struct net_if *iface) {
  int i = 0;

  for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
    char buf[NET_IPV4_ADDR_LEN];

    if (iface->config.ip.ipv4->unicast[i].addr_type != NET_ADDR_DHCP) {
      continue;
    }

    printk("IPv4 address: %s\n",
           net_addr_ntop(AF_INET,
                         &iface->config.ip.ipv4->unicast[i].address.in_addr,
                         buf, sizeof(buf)));
    printk("Subnet: %s\n",
           net_addr_ntop(AF_INET, &iface->config.ip.ipv4->netmask, buf,
                         sizeof(buf)));
    printk("Router: %s\n", net_addr_ntop(AF_INET, &iface->config.ip.ipv4->gw,
                                         buf, sizeof(buf)));
  }

  // k_sem_give(&ipv4_address_obtained);
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint32_t mgmt_event, struct net_if *iface) {
  switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
      handle_wifi_connect_result(cb);
      break;

    case NET_EVENT_WIFI_DISCONNECT_RESULT:
      handle_wifi_disconnect_result(cb);
      break;

    case NET_EVENT_IPV4_ADDR_ADD:
      handle_ipv4_result(iface);
      break;

    default:
      break;
  }
}

void wifi_status(void) {
  struct net_if *iface = net_if_get_default();

  struct wifi_iface_status status = {0};

  if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
               sizeof(struct wifi_iface_status))) {
    printk("WiFi Status Request Failed\n");
  }

  printk("\n");

  if (status.state >= WIFI_STATE_ASSOCIATED) {
    printk("SSID: %-32s\n", status.ssid);
    printk("Band: %s\n", wifi_band_txt(status.band));
    printk("Channel: %d\n", status.channel);
    printk("Security: %s\n", wifi_security_txt(status.security));
    printk("RSSI: %d\n", status.rssi);
  }
}

int wifi_sta_connect() {
  struct net_if *iface = net_if_get_first_wifi();

  if (iface == NULL) {
    printk("No interface found. Exit.\n");
    return -1;
  } else {
    printk("Interface found.\n");
  }

  static struct wifi_connect_req_params cnx_params;

  cnx_params.channel = WIFI_CHANNEL_ANY;
  cnx_params.ssid = SSID;
  cnx_params.ssid_length = strlen(SSID);
  cnx_params.psk = PASSWORD;
  cnx_params.psk_length = strlen(PASSWORD);
  // cnx_params.sae_password = PASSWORD;
  // cnx_params.sae_password_length = strlen(PASSWORD);
  cnx_params.security = 1;
  cnx_params.band = WIFI_FREQ_BAND_2_4_GHZ;
  cnx_params.mfp = WIFI_MFP_DISABLE;

  printk("Connecting to SSID: %s\n", cnx_params.ssid);

  if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params,
               sizeof(struct wifi_connect_req_params))) {
    printk("WiFi Connection Request Failed\n");
    return -ENETDOWN;
  }

  wifi_status();
  return 1;
}

int wifi_ap_enable() {
  struct net_if *iface = net_if_get_first_wifi();
  printk("Enter: wifi_ap_enable");

  if (iface == NULL) {
    printk("No interface found. Exit");
    return -1;
  } else {
    printk("Interface found.");
  }

  static struct wifi_connect_req_params cnx_params;

  cnx_params.band = WIFI_FREQ_BAND_UNKNOWN;
  cnx_params.channel = WIFI_CHANNEL_ANY;
  cnx_params.ssid = SSID;
  cnx_params.ssid_length = strlen(SSID);
  cnx_params.security = WIFI_SECURITY_TYPE_NONE;

  int ret;
  printk("Trying to enter AP mode for %s\n", iface->if_dev->dev->name);
  ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &cnx_params,
                 sizeof(struct wifi_connect_req_params));
  if (ret) {
    printk("AP mode enable failed: %s\n", strerror(-ret));
    return -ENETDOWN;
  }

  printk("AP mode enabled. SSID: %s\n", SSID);

  return 1;
}

int wifi_ap_disable() {
  struct net_if *iface = net_if_get_first_wifi();
  int ret;

  ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
  if (ret) {
    printk("AP mode disable failed: %s\n", strerror(-ret));
    return -ENETDOWN;
  }

  printk("AP mode disabled\n");

  return 0;
}

int wifi_init() {
  printk("WiFi Example\nBoard: %s\n", CONFIG_BOARD);

  net_mgmt_init_event_callback(
      &wifi_cb, wifi_mgmt_event_handler,
      NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);

  net_mgmt_init_event_callback(&ipv4_cb, wifi_mgmt_event_handler,
                               NET_EVENT_IPV4_ADDR_ADD);

  net_mgmt_add_event_callback(&wifi_cb);
  // net_mgmt_add_event_callback(&ipv4_cb);
  printk("WiFi Init Complete\n");

  return 0;
}
