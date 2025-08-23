#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_ip.h>

#include "wifi.h"

// #define SSID "ALHN-A898"
// #define PASSWORD "XA5nh7pZXf"

#define SSID "VM3517472"
#define PASSWORD "bts7Np7mcyMw"

#define WIFI_RECONNECT_DELAY_MS 5000
#define WIFI_DHCP_RENEW_INTERVAL_MS (60 * 60 * 1000) // 1 hour

LOG_MODULE_REGISTER(wifi, CONFIG_LOG_DEFAULT_LEVEL);

static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct net_mgmt_event_callback ip_mgmt_cb;
static struct k_work_delayable wifi_reconnect_work;
static struct k_work_delayable wifi_dhcp_renew_work;

static void wifi_schedule_dhcp_renew(void);

static void schedule_wifi_reconnect(void)
{
  LOG_INF("Scheduling WiFi reconnection in %d ms", WIFI_RECONNECT_DELAY_MS);
  k_work_schedule(&wifi_reconnect_work, K_MSEC(WIFI_RECONNECT_DELAY_MS));
}

static void wifi_reconnect_work_handler(struct k_work *work)
{
  LOG_INF("Attempting to reconnect to WiFi network");
  wifi_sta_connect();
}

static void wifi_dhcp_renew_work_handler(struct k_work *work)
{
  struct net_if *iface = net_if_get_first_wifi();

  if (!iface)
  {
    LOG_ERR("Failed to get default network interface for DHCP renew");
    return;
  }

  LOG_INF("Requesting DHCP renewal");
  net_dhcpv4_start(iface);
  LOG_INF("DHCP renewal request sent");

  wifi_schedule_dhcp_renew();
}

static void wifi_schedule_dhcp_renew(void)
{
  LOG_INF("Scheduling next DHCP renewal in %d ms", WIFI_DHCP_RENEW_INTERVAL_MS);
  k_work_schedule(&wifi_dhcp_renew_work, K_MSEC(WIFI_DHCP_RENEW_INTERVAL_MS));
}

static void handle_wifi_connect_result(const struct wifi_status *status)
{
  if (!status)
  {
    LOG_ERR("WiFi connect result: NULL status received");
    return;
  }

  if (status->status)
  {
    LOG_ERR("Connection request failed with status code: %d", status->status);
    schedule_wifi_reconnect();
  }
  else
  {
    struct net_if *iface = net_if_get_first_wifi();

    LOG_INF("Successfully connected to WiFi network");
    wifi_status();
    net_dhcpv4_start(iface);
    wifi_schedule_dhcp_renew();
  }
}

static void handle_wifi_disconnect_result(const struct wifi_status *status)
{
  if (!status)
  {
    LOG_ERR("WiFi disconnect result: NULL status received");
    return;
  }

  if (status->status)
  {
    LOG_WRN("Disconnection requested with status code: %d", status->status);
  }
  else
  {
    LOG_INF("Disconnected from WiFi network");
  }

  schedule_wifi_reconnect();
}

static void handle_ipv4_result(struct net_if *iface)
{
  if (!iface || !iface->config.ip.ipv4)
  {
    LOG_ERR("Invalid iface or ipv4 config");
    return;
  }

  LOG_INF("Processing IPv4 address assignment for interface: %p", iface);

  for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++)
  {
    const struct net_if_addr_ipv4 *ipv4_addr = &iface->config.ip.ipv4->unicast[i];
    char buf[NET_IPV4_ADDR_LEN];

    LOG_INF("IPv4 Address: %s", net_addr_ntop(AF_INET, &ipv4_addr->ipv4.address.in_addr.s4_addr, buf, sizeof(buf)));
    struct in_addr netmask = net_if_ipv4_get_netmask_by_addr(iface, &ipv4_addr->ipv4.address.in_addr);
    LOG_INF("Subnet Mask: %s", net_addr_ntop(AF_INET, &netmask, buf, sizeof(buf)));
    LOG_INF("Default Gateway: %s", net_addr_ntop(AF_INET, &iface->config.ip.ipv4->gw, buf, sizeof(buf)));
  }
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface)
{
  if (!cb)
  {
    LOG_ERR("Net management event callback is NULL");
    return;
  }

  switch (mgmt_event)
  {
  case NET_EVENT_WIFI_CONNECT_RESULT:
    LOG_INF("Received NET_EVENT_WIFI_CONNECT_RESULT");
    handle_wifi_connect_result((const struct wifi_status *)cb->info);
    break;
  case NET_EVENT_WIFI_DISCONNECT_RESULT:
    LOG_INF("Received NET_EVENT_WIFI_DISCONNECT_RESULT");
    handle_wifi_disconnect_result((const struct wifi_status *)cb->info);
    break;
  default:
    LOG_WRN("Unhandled wifi event: 0x%x", mgmt_event);
    break;
  }
}

static void ip_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface)
{
  LOG_INF("ip_mgmt_event_handler called with event: 0x%x", mgmt_event);

  if (!cb)
  {
    LOG_ERR("Net management event callback is NULL");
    return;
  }
  
  switch (mgmt_event)
  {
  case NET_EVENT_IPV4_ADDR_ADD:
  case NET_EVENT_IPV4_DHCP_BOUND:
    LOG_INF("Received NET_EVENT_IPV4 address event");
    handle_ipv4_result(iface);
    break;
  case NET_EVENT_IPV4_ADDR_DEL:
    LOG_WRN("IPv4 address was removed! Scheduling reconnect.");
    schedule_wifi_reconnect();
    break;
  case NET_EVENT_IPV4_DHCP_STOP:
    LOG_WRN("DHCP client stopped unexpectedly! Scheduling reconnect.");
    schedule_wifi_reconnect();
    break;
  case NET_EVENT_IPV4_CMD_DHCP_START:
    LOG_INF("Started DHCP client successfully.");
    break;
  default:
    LOG_WRN("Unhandled IP event: 0x%x", mgmt_event);
    break;
  }
}

void wifi_status(void)
{
  struct net_if *iface = net_if_get_default();
  struct wifi_iface_status status = {0};

  if (!iface)
  {
    LOG_ERR("Failed to get default network interface");
    return;
  }

  if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status)))
  {
    LOG_ERR("WiFi Status Request Failed");
    return;
  }

  if (status.state >= WIFI_STATE_ASSOCIATED)
  {
    LOG_INF("WiFi Interface Status:");
    LOG_INF("SSID: %-32s", status.ssid);
    LOG_INF("Band: %s", wifi_band_txt(status.band));
    LOG_INF("Channel: %d", status.channel);
    LOG_INF("Security: %s", wifi_security_txt(status.security));
    LOG_INF("RSSI: %d dBm", status.rssi);
  }
  else
  {
    LOG_INF("WiFi not associated yet");
  }
}

static int wifi_sta_connect(void)
{
  struct net_if *iface = net_if_get_first_wifi();

  if (!iface)
  {
    LOG_ERR("No WiFi interface found. Aborting connection attempt.");
    return -ENODEV;
  }

  static struct wifi_connect_req_params cnx_params = {0};

  cnx_params.channel = WIFI_CHANNEL_ANY;
  cnx_params.ssid = SSID;
  cnx_params.ssid_length = strlen(SSID);
  cnx_params.psk = PASSWORD;
  cnx_params.psk_length = strlen(PASSWORD);
  cnx_params.security = WIFI_SECURITY_TYPE_PSK;
  cnx_params.band = WIFI_FREQ_BAND_2_4_GHZ;
  cnx_params.mfp = WIFI_MFP_DISABLE;

  LOG_INF("Initiating connection to SSID: %s", cnx_params.ssid);

  if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params, sizeof(cnx_params)))
  {
    LOG_ERR("WiFi connection request failed");
    return -ENETDOWN;
  }

  return 0;
}

int wifi_init(void)
{
  LOG_INF("Initializing WiFi subsystem on board: %s", CONFIG_BOARD);

  net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler,
                               NET_EVENT_WIFI_CONNECT_RESULT |
                                   NET_EVENT_WIFI_DISCONNECT_RESULT);
  net_mgmt_add_event_callback(&wifi_mgmt_cb);

  net_mgmt_init_event_callback(&ip_mgmt_cb, ip_mgmt_event_handler,
                               NET_EVENT_IPV4_ADDR_ADD |
                                   NET_EVENT_IPV4_ADDR_DEL |
                                   NET_EVENT_IPV4_DHCP_STOP |
                                   NET_EVENT_IPV4_DHCP_BOUND);
  net_mgmt_add_event_callback(&ip_mgmt_cb);

  k_work_init_delayable(&wifi_reconnect_work, wifi_reconnect_work_handler);
  k_work_init_delayable(&wifi_dhcp_renew_work, wifi_dhcp_renew_work_handler);

  k_sleep(K_MSEC(500));

  wifi_sta_connect();

  LOG_INF("WiFi subsystem initialization complete");
  return 0;
}