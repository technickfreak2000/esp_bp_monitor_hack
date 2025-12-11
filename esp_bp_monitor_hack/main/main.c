#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "protocol_examples_common.h"
#include "esp_ota_ops.h"

#include <esp_http_server.h>

#include "spiffs.h"
#include "ws_server.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "display.h"
#include "gpio.h"

static const char *TAG = "main";

void app_main(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running)
    {
        ESP_LOGE("ota_info",
                 "Booted from partition label=\"%s\", "
                 "type=0x%02x, subtype=0x%02x, "
                 "address=0x%08" PRIx32,
                 running->label,
                 (unsigned)running->type,
                 (unsigned)running->subtype,
                 running->address);
    }
    else
    {
        ESP_LOGE("ota_info", "esp_ota_get_running_partition() returned NULL!");
    }

    init_gpio();

    u8g2_init_display();

    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    mount_spiffs();

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    /* Start the server for the first time */
    server = start_webserver();
}
