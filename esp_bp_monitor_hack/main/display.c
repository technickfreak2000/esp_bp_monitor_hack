#include "display.h"
#include "u8g2_esp32_hal.h"
#include "u8g2.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <esp_log.h>

static const char *TAG = "display";

#define PIN_SDA 17
#define PIN_SCL 16
u8g2_t u8g2;

void u8g2_init_display()
{
    display_mutex = xSemaphoreCreateMutex();
    if (display_mutex == NULL)
    {
        // handle error: out of memory
        ESP_LOGE(TAG, "Failed to create display_mutex");
        abort();
    }

    xSemaphoreTake(display_mutex, portMAX_DELAY);
    /*
        Display is a 0.96 Inch 128 x 64 Pixel OLED with a 1315 Chip and two color
        top part is yellow,  0-15px
        bottom part is blue, 16-63
    */
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = PIN_SDA;
    u8g2_esp32_hal.bus.i2c.scl = PIN_SCL;
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &u8g2,
        U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);

    // SSD1306 expects address in 8-bit format: 0x3C << 1 = 0x78
    u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);

    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_ClearBuffer(&u8g2);
    xSemaphoreGive(display_mutex);

    // update_measurement(59, 83, 23, false);
    draw_measuring();
    xTaskCreate(task_snake, "task_snake", 4096, NULL, 4, NULL);
}

void update_display()
{
    void (*func)(void);

    portENTER_CRITICAL(&display_mux);
    func = previously_displayed;
    portEXIT_CRITICAL(&display_mux);

    xSemaphoreTake(display_mutex, portMAX_DELAY);
    if (func)
        func();
    xSemaphoreGive(display_mutex);
}

static void add_head(u8g2_t *u8g2)
{
    // Clock top left
    u8g2_SetFont(u8g2, u8g2_font_timR14_tr);
    u8g2_DrawStr(u8g2, 0, 14, "00");
    u8g2_DrawStr(u8g2, 23, 14, "00");
    u8g2_DrawStr(u8g2, 19, 12, ":");

    // Bluetooth
    u8g2_DrawXBM(u8g2, 52, 2, 7, 10, image_Layer_32_bits);
    // Bluetooth connected
    u8g2_DrawXBM(u8g2, 51, 5, 10, 4, image_Layer_41_bits);

    // WiFi
    u8g2_DrawXBM(u8g2, 67, 2, 15, 10, image_Layer_31_bits);

    // Battery
    // Symbol
    u8g2_DrawLine(u8g2, 90, 5, 90, 8);
    u8g2_DrawLine(u8g2, 93, 4, 91, 4);
    u8g2_DrawLine(u8g2, 93, 1, 93, 3);
    u8g2_DrawLine(u8g2, 126, 0, 94, 0);
    u8g2_DrawLine(u8g2, 127, 1, 127, 12);
    u8g2_DrawLine(u8g2, 94, 13, 126, 13);
    u8g2_DrawLine(u8g2, 93, 10, 93, 12);
    u8g2_DrawLine(u8g2, 91, 9, 92, 9);
    // Percent
    u8g2_DrawLine(u8g2, 118, 10, 124, 4);
    u8g2_DrawEllipse(u8g2, 123, 9, 1, 1, U8G2_DRAW_ALL);
    u8g2_DrawEllipse(u8g2, 119, 5, 1, 1, U8G2_DRAW_ALL);
    // Value
    u8g2_SetFont(u8g2, u8g2_font_profont12_tr);
    u8g2_DrawStr(u8g2, 98, 11, "100");

    // Seperator
    u8g2_DrawLine(u8g2, 127, 15, 0, 15);
}

static void draw_snake(u8g2_t *u8g2) {
    int h = snake_head_idx, len = SNAKE_PATH_LEN;
    // head
    u8g2_DrawXBM(u8g2, snake_path[h].x, snake_path[h].y, 7,7, head_bm[snake_path[h].dir]);
    // body
    for (int s=1; s<=snake_body_sections; s++) {
        int bi = (h - s + len) % len;
        uint8_t bx = snake_path[bi].x, by = snake_path[bi].y;
        if (snake_wiggle && (s & 1)) {
            if (snake_path[bi].dir == DIR_RIGHT || snake_path[bi].dir == DIR_LEFT) by++;
            else bx++;
        }
        u8g2_DrawXBM(u8g2, bx, by, 5,5, body_bm[snake_path[bi].dir]);
    }
    // tail
    int ti = (h - snake_body_sections - 1 + len) % len;
    u8g2_DrawXBM(u8g2, snake_path[ti].x, snake_path[ti].y, 5,5, tail_bm[snake_path[ti].dir]);
}

void draw_error()
{
    portENTER_CRITICAL(&display_mux);
    previously_displayed = draw_error;
    portEXIT_CRITICAL(&display_mux);

    u8g2_ClearBuffer(&u8g2);
    u8g2_SetBitmapMode(&u8g2, 1);
    u8g2_SetFontMode(&u8g2, 1);

    add_head(&u8g2);

    // Menu
    u8g2_DrawXBM(&u8g2, 0, 30, 16, 16, image_menu_settings_gear_bits);
    u8g2_DrawXBM(&u8g2, 116, 29, 12, 16, image_operation_undo_bits);

    u8g2_SendBuffer(&u8g2);
}

void draw_no_measurement()
{
    portENTER_CRITICAL(&display_mux);
    previously_displayed = draw_no_measurement;
    portEXIT_CRITICAL(&display_mux);

    u8g2_ClearBuffer(&u8g2);
    u8g2_SetBitmapMode(&u8g2, 1);
    u8g2_SetFontMode(&u8g2, 1);

    add_head(&u8g2);

    // Menu
    u8g2_DrawXBM(&u8g2, 0, 30, 16, 16, image_menu_settings_gear_bits);
    u8g2_DrawXBM(&u8g2, 116, 29, 12, 16, image_operation_undo_bits);

    u8g2_SendBuffer(&u8g2);
}

void draw_measuring()
{
    portENTER_CRITICAL(&display_mux);
    previously_displayed = draw_measuring;
    portEXIT_CRITICAL(&display_mux);

    u8g2_ClearBuffer(&u8g2);
    u8g2_SetBitmapMode(&u8g2, 1);
    u8g2_SetFontMode(&u8g2, 1);

    add_head(&u8g2);

    // Menu
    // u8g2_DrawXBM(&u8g2, 0, 30, 16, 16, image_menu_settings_gear_bits);
    // u8g2_DrawXBM(&u8g2, 116, 29, 12, 16, image_operation_undo_bits);

    u8g2_SetFont(&u8g2, u8g2_font_profont15_tr);
    u8g2_DrawStr(&u8g2, 3, 27, "Measuring");
    
    u8g2_DrawXBM(&u8g2, 96, 17, 15, 16, image_cards_hearts_bits);

    u8g2_SetFont(&u8g2, u8g2_font_profont29_tr);
    u8g2_DrawStr(&u8g2, 80, 56, "9");
    u8g2_DrawStr(&u8g2, 96, 56, "9");
    u8g2_DrawStr(&u8g2, 112, 56, "9");

    draw_snake(&u8g2);

    u8g2_SendBuffer(&u8g2);
}

void update_measurement(uint16_t new_systole, uint16_t new_diastole, uint16_t new_pulse, bool new_arrhythmia)
{
    uint16_t local_map, local_map_v1, local_map_v2, local_map_v3;
    portENTER_CRITICAL(&display_mux);

    systole = new_systole;
    diastole = new_diastole;
    pulse = new_pulse;
    arrhythmia = new_arrhythmia;

    map = (2 * diastole + systole) / 3;

    local_map = map;
    local_map_v1 = map_v1;
    local_map_v2 = map_v2;
    local_map_v3 = map_v3;
    portEXIT_CRITICAL(&display_mux);

    if (map >= map_v1 && map < map_v3)
    {
        if (xTaskGetHandle("task_blink_warn") != NULL)
        {
            vTaskDelete(xTaskGetHandle("task_blink_warn"));
        }
        portENTER_CRITICAL(&display_mux);
        warning = true;
        portEXIT_CRITICAL(&display_mux);
    }
    else if (map >= map_v3)
    {
        if (xTaskGetHandle("task_blink_warn") == NULL)
        {
            portENTER_CRITICAL(&display_mux);
            previously_displayed = draw_measurement;
            portEXIT_CRITICAL(&display_mux);
            xTaskCreate(task_blink_warn, "task_blink_warn", 4096, NULL, 4, NULL);
        }
    }
    else
    {
        if (xTaskGetHandle("task_blink_warn") != NULL)
        {
            vTaskDelete(xTaskGetHandle("task_blink_warn"));
        }
        portENTER_CRITICAL(&display_mux);
        warning = false;
        portEXIT_CRITICAL(&display_mux);
    }

    xSemaphoreTake(display_mutex, portMAX_DELAY);
    draw_measurement();
    xSemaphoreGive(display_mutex);
}

void draw_measurement()
{
    portENTER_CRITICAL(&display_mux);
    previously_displayed = draw_measurement;
    portEXIT_CRITICAL(&display_mux);

    u8g2_ClearBuffer(&u8g2);
    u8g2_SetBitmapMode(&u8g2, 1);
    u8g2_SetFontMode(&u8g2, 1);

    add_head(&u8g2);

    char (*return_values)[2] = NULL;

    // Menu
    u8g2_DrawXBM(&u8g2, 0, 30, 16, 16, image_menu_settings_gear_bits);
    u8g2_DrawXBM(&u8g2, 116, 29, 12, 16, image_operation_undo_bits);

    // SYS Value
    u8g2_SetFont(&u8g2, u8g2_font_profont29_tr);
    uint16_t local_systole;
    portENTER_CRITICAL(&display_mux);
    local_systole = systole;
    portEXIT_CRITICAL(&display_mux);
    return_values = three_digit_handle(local_systole);
    u8g2_DrawStr(&u8g2, 18, 37, return_values[0]);
    u8g2_DrawStr(&u8g2, 34, 37, return_values[1]);
    u8g2_DrawStr(&u8g2, 50, 37, return_values[2]);

    // DIA Value
    u8g2_SetFont(&u8g2, u8g2_font_profont22_tr);
    uint16_t local_diastole;
    portENTER_CRITICAL(&display_mux);
    local_diastole = diastole;
    portEXIT_CRITICAL(&display_mux);
    return_values = three_digit_handle(local_diastole);
    u8g2_DrawStr(&u8g2, 29, 53, return_values[0]);
    u8g2_DrawStr(&u8g2, 41, 53, return_values[1]);
    u8g2_DrawStr(&u8g2, 53, 53, return_values[2]);

    // PULSE Value
    u8g2_SetFont(&u8g2, u8g2_font_profont15_tr);
    uint16_t local_pulse;
    portENTER_CRITICAL(&display_mux);
    local_pulse = pulse;
    portEXIT_CRITICAL(&display_mux);
    return_values = three_digit_handle(local_pulse);
    u8g2_DrawStr(&u8g2, 43, 64, return_values[0]);
    u8g2_DrawStr(&u8g2, 50, 64, return_values[1]);
    u8g2_DrawStr(&u8g2, 57, 64, return_values[2]);

    // MAP Value
    u8g2_SetFont(&u8g2, u8g2_font_profont10_tr);
    u8g2_DrawStr(&u8g2, 102, 24, "MAP");
    uint16_t local_map;
    portENTER_CRITICAL(&display_mux);
    local_map = map;
    portEXIT_CRITICAL(&display_mux);
    return_values = three_digit_handle(local_map);
    u8g2_DrawStr(&u8g2, 86, 24, return_values[0]);
    u8g2_DrawStr(&u8g2, 91, 24, return_values[1]);
    u8g2_DrawStr(&u8g2, 96, 24, return_values[2]);

    // MAP bar
    u8g2_DrawXBM(&u8g2, 80, 32, 30, 5, image_Layer_35_bits);
    // MAP Pointer
    uint16_t local_v0, local_v1, local_v2, local_v3, local_v4;
    portENTER_CRITICAL(&display_mux);
    local_v0 = map_v0;
    local_v1 = map_v1;
    local_v2 = map_v2;
    local_v3 = map_v3;
    local_v4 = map_v4;
    portEXIT_CRITICAL(&display_mux);
    uint8_t off;
    if (local_map <= local_v0)
    {
        off = 0; // at or below left bound
    }
    else if (local_map >= local_v4)
    {
        off = 29; // at or above right bound (30px wide → index 29)
    }
    else if (local_map < local_v1)
    {
        // segment0: [v0…v1) → pixels 0…8 (9px)
        off = ((uint32_t)(local_map - local_v0) * 9) / (local_v1 - local_v0);
    }
    else if (local_map < local_v2)
    {
        // segment1: [v1…v2) → pixels 10…15
        off = 9 + 1 + ((uint32_t)(local_map - local_v1) * 6) / (local_v2 - local_v1);
    }
    else if (local_map < local_v3)
    {
        // segment2: [v2…v3) → pixels 17…22
        off = 9 + 1 + 6 + 1 + ((uint32_t)(local_map - local_v2) * 6) / (local_v3 - local_v2);
    }
    else
    {
        // segment3: [v3…v4] → pixels 24…29
        off = 9 + 1 + 6 + 1 + 6 + 1 + ((uint32_t)(local_map - local_v3) * 6) / (local_v4 - local_v3);
    }
    // Draw the 5×5 pointer at X = 80 + off, Y = 26
    u8g2_DrawXBM(&u8g2, 78 + off, 26, 5, 5, image_Layer_37_bits);

    bool local_warning, local_arrhythmia;
    portENTER_CRITICAL(&display_mux);
    local_warning = warning;
    local_arrhythmia = arrhythmia;
    portEXIT_CRITICAL(&display_mux);

    // !
    if (local_warning)
    {
        u8g2_DrawXBM(&u8g2, 67, 18, 10, 19, image_Layer_34_bits);
    }

    // Heart
    // Icon
    u8g2_DrawXBM(&u8g2, 81, 42, 15, 16, image_cards_hearts_bits);
    // Irregular
    if (local_arrhythmia)
    {
        u8g2_DrawXBM(&u8g2, 77, 40, 37, 20, image_Layer_34_1_bits);
    }

    u8g2_SendBuffer(&u8g2);
}

void task_blink_warn()
{
    while (1)
    {
        void (*func)(void);

        portENTER_CRITICAL(&display_mux);
        func = previously_displayed;
        portEXIT_CRITICAL(&display_mux);

        if (func != draw_measurement)
        {
            vTaskDelete(NULL);
        }

        portENTER_CRITICAL(&display_mux);
        warning = !warning;
        portEXIT_CRITICAL(&display_mux);

        update_display();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void task_snake()
{
    while (1)
    {
        void (*func)(void);

        portENTER_CRITICAL(&display_mux);
        func = previously_displayed;
        portEXIT_CRITICAL(&display_mux);

        if (func != draw_measuring)
        {
            vTaskDelete(NULL);
        }

        snake_head_idx = (snake_head_idx + 1) % SNAKE_PATH_LEN;
        snake_wiggle   = !snake_wiggle;

        update_display();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

char (*three_digit_handle(uint16_t value))[2]
{
    static char result[3][2] = {{'\0', '\0'},
                                {'\0', '\0'},
                                {'\0', '\0'}};

    // 1) Clamp to 0…999
    if (value > 999)
    {
        value = 999;
    }

    // 2) Convert to string in buf ("0"… "999")
    char buf[4];
    snprintf(buf, sizeof(buf), "%u", value);
    size_t len = strlen(buf);

    // 3) Initialize result[] to empty strings
    result[0][0] = '\0';
    result[1][0] = '\0';
    result[2][0] = '\0';

    // 4) Right-justify: place 1, 2, or 3 digits into result[] slots
    if (len == 3)
    {
        result[0][0] = buf[0];
        result[1][0] = buf[1];
        result[2][0] = buf[2];
    }
    else if (len == 2)
    {
        // only two digits → put them in result[1] & result[2]
        result[1][0] = buf[0];
        result[2][0] = buf[1];
    }
    else if (len == 1)
    {
        // only one digit → put it in result[2]
        result[2][0] = buf[0];
    }
    // if len == 0 (shouldn't happen), all remain ""

    // Ensure NUL-termination (we only wrote [0], so [1] is already '\0')
    result[0][1] = '\0';
    result[1][1] = '\0';
    result[2][1] = '\0';

    return result;
}
