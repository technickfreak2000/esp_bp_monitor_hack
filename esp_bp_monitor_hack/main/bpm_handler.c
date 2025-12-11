#include "bpm_handler.h"
#include "driver/gpio.h"


#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include "gpio.h"
#include "ws_server.h"

static const char *TAG = "BPM_HANDLER";

void start_measurement(httpd_user_t *httpd_user)
{
    // Check if measurement is already in progress
    TaskHandle_t xHandle = xTaskGetHandle("take_meas");
    if (xHandle != NULL) {
        ESP_LOGE(TAG, "Measurement is already running");
        return;
    }

    xTaskCreate(task_take_measurement, "take_meas", 4096, httpd_user, 5, NULL);
}

void stop_measurement()
{
}

static void task_take_measurement(void *param_httpd_user)
{
    httpd_user_t *httpd_user = (httpd_user_t *)param_httpd_user;

    if (httpd_user->server == NULL || httpd_user->ws_client == NULL) {
        ESP_LOGE(TAG, "HTTPD user or server is NULL - Server: %p, Client: %d", httpd_user->server, httpd_user->ws_client);
    }
    ESP_LOGE(TAG, "HTTPD - Server: %p, Client: %d", httpd_user->server, httpd_user->ws_client);

    ESP_LOGI(TAG, "Starting measurement");
    // ESP_ERROR_CHECK(trigger_async_send(httpd_user->server, httpd_user->ws_client, "Starting measurement"));
    
    // ESP_ERROR_CHECK(trigger_async_send(httpd_user->server, httpd_user->ws_client, "powring up blood pressure monitor"));
    set_bpm_power(true);
    // !!! dont forget to implement to perform self test here
    vTaskDelay(pdMS_TO_TICKS(500)); // Wait for 0.5 second to ensure power is stable
    ESP_LOGI(TAG, "Triggering blood pressure measurement");
    // ESP_ERROR_CHECK(trigger_async_send(httpd_user->server, httpd_user->ws_client, "Triggering blood pressure measurement"));
    trigger_bpm_measurement();
    ESP_LOGI(TAG, "Measurement in progress");
    // ESP_ERROR_CHECK(trigger_async_send(httpd_user->server, httpd_user->ws_client, "Measurement in progress"));

    // set interrupt on gpio

    // set_multiplexer(0, 1); 
    // set_multiplexer(1, 4); 

    int toggle = 0;
    int toggle_2 = 0;
    int toggle_3 = 0;
    for (size_t i = 0; i < 500; i++)
    {
        //int solenoid_level = gpio_get_level(GPIO_BPM_SOLENOID); 
        // ESP_LOGE(TAG, "Solenoid level: %d", solenoid_level);
        int lcd_data_1 = gpio_get_level(GPIO_MULTIPLEXER_1_LCD_DATA);
        int lcd_data_2 = gpio_get_level(GPIO_MULTIPLEXER_2_LCD_DATA);
        int lcd_common = gpio_get_level(GPIO_MULTIPLEXER_0_LCD_COMMON);
        if (lcd_data_1 == 1)
        {
            toggle = 1;
        }
        if (lcd_data_2 == 1)
        {
            toggle_2 = 1;
        }
        if (lcd_common == 1) // Replace with actual condition for toggle_3
        {
            toggle_3 = 1;
        }

        ESP_LOGE(TAG, "LCD Data 1: %d, LCD Data 2: %d, LCD Common: %d", lcd_data_1, lcd_data_2, lcd_common);
        vTaskDelay(pdMS_TO_TICKS(100));
    }


    ESP_LOGW(TAG, "Toggle: %d, Toggle 2: %d, Toggle 3: %d", toggle, toggle_2, toggle_3);
    // set_bpm_power(false);
    ESP_LOGI(TAG, "Measurement completed");
    //ESP_ERROR_CHECK(trigger_async_send(httpd_user->server, httpd_user->ws_client, "Measurement completed"));

    free(httpd_user); // Free the httpd_user structure

    vTaskDelete(NULL);
}