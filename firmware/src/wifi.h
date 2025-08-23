
#include <zephyr/net/net_if.h>

#ifdef __cplusplus
extern "C"
{
#endif

    static int wifi_sta_connect(void);
    static void handle_ipv4_result(struct net_if *iface);
    int wifi_init();
    void wifi_status();

#ifdef __cplusplus
}
#endif