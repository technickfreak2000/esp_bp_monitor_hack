#include "gpio.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GPIO";

void init_gpio()
{
    // Initialize output GPIOs
    // zero-initialize the config structure.
    gpio_config_t io_conf = {};
    // disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    // bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    // disable pull-down mode
    io_conf.pull_down_en = 0;
    // disable pull-up mode
    io_conf.pull_up_en = 0;
    // configure GPIO with the given settings
    gpio_config(&io_conf);

    // Initialize input GPIOs
    // disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    // bit mask of the pins that you want to set,e.g.GPIO36/39/34
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    // disable pull-down mode
    io_conf.pull_down_en = 0;
    // disable pull-up mode
    io_conf.pull_up_en = 0;
    // configure GPIO with the given settings
    gpio_config(&io_conf);

    // Set initial state for output GPIOs
    gpio_set_level(GPIO_MULTIPLEXER_0_ADDR_0, 0);
    gpio_set_level(GPIO_MULTIPLEXER_0_ADDR_1, 0);
    gpio_set_level(GPIO_MULTIPLEXER_1_2_ADDR_0, 0);
    gpio_set_level(GPIO_MULTIPLEXER_1_2_ADDR_1, 0);
    gpio_set_level(GPIO_MULTIPLEXER_1_2_ADDR_2, 0);
    gpio_set_level(GPIO_MULTIPLEXER_1_2_ADDR_3, 0);
    gpio_set_level(GPIO_BPM_POWER, 0);
    gpio_set_level(GPIO_BPM_TRIGGER, 0);

    ESP_LOGI(TAG, "GPIOs initialized");
}

void set_multiplexer(uint8_t mux, uint8_t addr)
{
    if (mux == 0)
    {
        gpio_set_level(GPIO_MULTIPLEXER_0_ADDR_0, (addr & 0x01));
        gpio_set_level(GPIO_MULTIPLEXER_0_ADDR_1, (addr & 0x02) >> 1);
    }
    else if (mux == 1 || mux == 2)
    {
        gpio_set_level(GPIO_MULTIPLEXER_1_2_ADDR_0, (addr & 0x01));
        gpio_set_level(GPIO_MULTIPLEXER_1_2_ADDR_1, (addr & 0x02) >> 1);
        gpio_set_level(GPIO_MULTIPLEXER_1_2_ADDR_2, (addr & 0x04) >> 2);
        gpio_set_level(GPIO_MULTIPLEXER_1_2_ADDR_3, (addr & 0x08) >> 3);
    }
}

void set_bpm_power(bool on)
{
    // Ensure trigger is "off" before powering on
    gpio_set_level(GPIO_BPM_TRIGGER, on ? 1 : 0); 

    gpio_set_level(GPIO_BPM_POWER, on ? 1 : 0);
}

void trigger_bpm_measurement()
{
    gpio_set_level(GPIO_BPM_TRIGGER, 0);
    vTaskDelay(pdMS_TO_TICKS(200)); // Trigger pulse duration
    gpio_set_level(GPIO_BPM_TRIGGER, 1);
}