/*
    GPIO hardware connections:
    - GPIO36 - multiplexer 0 - LCD panel common pins
    - GPIO39 - multiplexer 1 - LCD panel data pins 1
    - GPIO34 - multiplexer 2 - LCD panel data pins 2
    - GPIO35 - Vbat sens
    - GPIO32 - multiplexer 0 - address 0
    - GPIO33 - multiplexer 0 - address 1
    - GPIO25 - multiplexer 1&2 - address 0
    - GPIO26 - multiplexer 1&2 - address 1
    - GPIO27 - multiplexer 1&2 - address 2
    - GPIO14 - Button SET
    - GPIO12
    - GPIO13 - multiplexer 1&2 - address 3
    - GPIO15
    - GPIO0
    - GPIO2
    - GPIO4  - Button ON
    - GPIO16 - I2C SCL - OLED
    - GPIO17 - I2C SDA - OLED
    - GPIO5
    - GPIO18
    - GPIO19
    - GPIO21 - solenoid valve blood pressure monitor
    - GPIO22 - turn on/off blood pressure monitor
    - GPIO23 - trigger start measurement for blood pressure monitor
*/

#include <stdint.h>
#include <stdbool.h>

#ifndef GPIO_H
#define GPIO_H

// Inputs
//   Multiplexer common pins
#define GPIO_MULTIPLEXER_0_LCD_COMMON 36
#define GPIO_MULTIPLEXER_1_LCD_DATA 39
#define GPIO_MULTIPLEXER_2_LCD_DATA 34
//   Blood pressure monitor
#define GPIO_BPM_SOLENOID 21

// Outputs
//   Multiplexer control pins
#define GPIO_MULTIPLEXER_0_ADDR_0 32
#define GPIO_MULTIPLEXER_0_ADDR_1 33
#define GPIO_MULTIPLEXER_1_2_ADDR_0 25
#define GPIO_MULTIPLEXER_1_2_ADDR_1 26
#define GPIO_MULTIPLEXER_1_2_ADDR_2 27
#define GPIO_MULTIPLEXER_1_2_ADDR_3 13

//   Blood pressure monitor
#define GPIO_BPM_POWER 22
#define GPIO_BPM_TRIGGER 23
//   I2C OLED display
//   Display is configured in submodule

#define GPIO_INPUT_PIN_SEL ((1ULL << GPIO_MULTIPLEXER_0_LCD_COMMON) | \
                            (1ULL << GPIO_MULTIPLEXER_1_LCD_DATA) |   \
                            (1ULL << GPIO_MULTIPLEXER_2_LCD_DATA) |   \
                            (1ULL << GPIO_BPM_SOLENOID))

#define GPIO_OUTPUT_PIN_SEL ((1ULL << GPIO_MULTIPLEXER_0_ADDR_0) |   \
                             (1ULL << GPIO_MULTIPLEXER_0_ADDR_1) |   \
                             (1ULL << GPIO_MULTIPLEXER_1_2_ADDR_0) | \
                             (1ULL << GPIO_MULTIPLEXER_1_2_ADDR_1) | \
                             (1ULL << GPIO_MULTIPLEXER_1_2_ADDR_2) | \
                             (1ULL << GPIO_MULTIPLEXER_1_2_ADDR_3) | \
                             (1ULL << GPIO_BPM_TRIGGER) |            \
                             (1ULL << GPIO_BPM_POWER))

/**
 * @brief Initialize GPIO pins for the application.
 *
 * This function configures the GPIO pins as inputs or outputs based on their
 * intended usage. It sets up the pins for the multiplexer, blood pressure monitor,
 * and other peripherals.
 *
 * Usage:
 * Call this function at the start of the program to ensure GPIO pins are properly
 * configured before use.
 */
void init_gpio();

/**
 * @brief Set the address of a multiplexer.
 *
 * This function configures the address pins of the specified multiplexer to select
 * a particular channel. The multiplexer is identified by its index, and the address
 * is specified as a 4-bit value.
 *
 * @param mux The multiplexer index (0, 1, or 2).
 * @param addr The 4-bit address to set (0-15).
 *
 * Usage:
 * Use this function to control the multiplexer and select the desired channel.
 */
void set_multiplexer(uint8_t mux, uint8_t addr);

/**
 * @brief Turn the blood pressure monitor power on or off.
 *
 * This function controls the GPIO pin responsible for powering the blood pressure
 * monitor. Pass `true` to turn the power on, or `false` to turn it off.
 *
 * @param on Boolean value indicating whether to turn the power on (`true`) or off (`false`).
 *
 * Usage:
 * Use this function to manage the power state of the blood pressure monitor.
 */
void set_bpm_power(bool on);

/**
 * @brief Trigger a blood pressure measurement.
 *
 * This function sends a signal to the GPIO pin responsible for starting a blood
 * pressure measurement. Ensure the blood pressure monitor is powered on before
 * calling this function.
 *
 * Usage:
 * Call this function to initiate a measurement on the blood pressure monitor.
 */
void trigger_bpm_measurement();

#endif GPIO_H