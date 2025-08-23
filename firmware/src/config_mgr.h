#ifndef __GET_CONFIG_MGR__
#define __GET_CONFIG_MGR__
#include <stdbool.h>
#include <sys/types.h>

/* Config Keys */
#define CFG_REBOOTCNT_ID 1
#define CFG_DEVICEID_ID 2

#define CFG_SIZE_DEVICEID_ID 7

// #if defined(CONFIG_WIFI)
// #else
#define DEFAULT_DEVICE_ID "0f3def"
// #endif

#ifdef __cplusplus
extern "C"
{
#endif

    int cfg_init();
    void cfg_print();
    bool cfg_inc_boot();
    ssize_t cfg_get_value(int key, void *value, int len);
    ssize_t cfg_set_value(int key, void *value, int len);

#ifdef __cplusplus
}
#endif

#endif /*__GET_CONFIG_MGR__*/
