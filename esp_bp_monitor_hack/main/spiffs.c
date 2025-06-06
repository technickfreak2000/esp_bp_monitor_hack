#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"

#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "spiffs.h"

static const char *TAG = "spiffs_mount";

void mount_spiffs(void)
{
    // 1) Fill in the configuration structure
    esp_vfs_spiffs_conf_t conf = {
        .base_path      = "/spiffs",       // Where in the VFS API it will be mounted
        .partition_label = "storage",      // Must match the “label” field in your partition table
        .max_files      = 5,               // Max number of files open at the same time
        .format_if_mount_failed = false    // If true, SPIFFS will be formatted if mounting fails
    };

    // 2) Actually register/mount SPIFFS
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted at \"%s\" (label=\"%s\")",
                 conf.base_path, conf.partition_label);
    } else if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Failed to mount or format SPIFFS");
        return;
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to find SPIFFS partition with label \"%s\"",
                 conf.partition_label);
        return;
    } else {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)",
                 esp_err_to_name(ret));
        return;
    }

    // 3) (Optional) Query and print some info about the partition
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Partition size: total: %u bytes, used: %u bytes",
                 (unsigned) total, (unsigned) used);
    } else {
        ESP_LOGW(TAG, "Failed to get SPIFFS partition information (%s)",
                 esp_err_to_name(ret));
    }
}