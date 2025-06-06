#include "bpm_handler.h"
#include "driver/gpio.h"


#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>

void init_gpio()
{
}

void start_measurement()
{
    xTaskCreate(take_measurement, "take_measurement", 4096, NULL, 5, NULL);
}

void stop_measurement()
{
}

static void take_measurement()
{
    gpio_set_level(13, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(13, 1);

    vTaskDelete(NULL);
}