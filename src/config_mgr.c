#include "config_mgr.h"

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
LOG_MODULE_DECLARE(gs, CONFIG_GETSMART_LOG_LEVEL);

/* 1000 msec = 1 sec */
#define SLEEP_TIME 100

#define NVS_PARTITION storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)

/* Maximum length of srtings in config */
#define VALUE_STRING_MAXLEN 128

static struct nvs_fs fs;
static bool initialized = false;

char default_device_id[] = DEFAULT_DEVICE_ID;

// sys_rand_get(void *dst, size_t len)

static void cfg_set_defaults() {
  LOG_INF("Setting default config values.");
  uint32_t reboot_counter = 0U;
  int rc = 0;
  rc = cfg_get_value(CFG_REBOOTCNT_ID, &reboot_counter, sizeof(reboot_counter));
  if (rc <= 0) {
    LOG_INF("No Reboot counter found, adding it at slot %d\n",
            CFG_REBOOTCNT_ID);
    cfg_set_value(CFG_REBOOTCNT_ID, &reboot_counter, sizeof(reboot_counter));
  }

  char device_id[CFG_SIZE_DEVICEID_ID];
  rc = cfg_get_value(CFG_DEVICEID_ID, &device_id, CFG_SIZE_DEVICEID_ID);
  if (rc <= 0) {
    LOG_INF("No device id found, adding it at slot %d\n", CFG_DEVICEID_ID);
    cfg_set_value(CFG_DEVICEID_ID, &default_device_id,
                  strlen(default_device_id));
  }
}

int cfg_init() {
  if (initialized) {
    return -ENOTSUP;
  }
  int rc = 0;
  struct flash_pages_info info;

  /* define the nvs file system by settings with:
   *      sector_size equal to the pagesize,
   *      3 sectors
   *      starting at NVS_PARTITION_OFFSET
   */
  fs.flash_device = NVS_PARTITION_DEVICE;
  if (!device_is_ready(fs.flash_device)) {
    printk("Flash device %s is not ready\n", fs.flash_device->name);
    return -1;
  }
  fs.offset = NVS_PARTITION_OFFSET;
  rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
  if (rc) {
    printk("Unable to get page info\n");
    return -1;  // TODO
  }
  fs.sector_size = info.size;
  fs.sector_count = 4U;

  rc = nvs_mount(&fs);
  if (rc) {
    printk("Flash Init failed. ERRNO: %d\n", rc);
    return -1;  // TODO
  }
  cfg_set_defaults();
  initialized = true;
  return 0;
}

ssize_t cfg_get_value(int key, void* value, int len) {
  return nvs_read(&fs, key, value, len);
}

ssize_t cfg_set_value(int key, void* value, int len) {
  return nvs_write(&fs, key, value, len);
}

void cfg_print() {
  uint32_t reboot_counter = 0U;
  cfg_get_value(CFG_REBOOTCNT_ID, &reboot_counter, sizeof(reboot_counter));
  LOG_INF("Config Slot [%d] - CFG_REBOOTCNT_ID: %d", CFG_REBOOTCNT_ID,
          reboot_counter);

  char device_id[VALUE_STRING_MAXLEN];
  cfg_get_value(CFG_DEVICEID_ID, &device_id, VALUE_STRING_MAXLEN);
  LOG_INF("Config Slot [%d] - CFG_DEVICEID_ID: %s", CFG_DEVICEID_ID, device_id);
}

/* Increment the device boot count*/
bool cfg_inc_boot() {
  uint32_t reboot_counter = 0U;
  int rc = 0;
  rc = cfg_get_value(CFG_REBOOTCNT_ID, &reboot_counter, sizeof(reboot_counter));
  if (rc <= 0) {
    return false;
  }
  reboot_counter++;
  rc = cfg_set_value(CFG_REBOOTCNT_ID, &reboot_counter, sizeof(reboot_counter));
  if (rc <= 0) {
    return false;
  }
  return true;
}