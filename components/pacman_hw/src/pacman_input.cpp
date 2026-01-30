/*
 * pacman_input.cpp - Pac-Man Input Handling for FIESTA26
 *
 * Uses GPIO buttons and optionally IMU for tilt control
 */

#include "pacman_input.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PACMAN_INPUT";

// FIESTA26 button pins
#define PIN_BTN_BOOT    GPIO_NUM_9    // BOOT button
#define PIN_BTN_PWR     GPIO_NUM_18   // PWR button

// Current input state
static uint8_t current_buttons = 0;

// Virtual coin/start state machine (single button triggers both)
static uint32_t virtual_coin_timer = 0;
static int virtual_coin_state = 0;

void pacman_input_init(void)
{
    ESP_LOGI(TAG, "Initializing input");

    // Configure button GPIOs
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_PWR);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Input initialized (BOOT=coin/start, PWR=fire/direction)");
}

void pacman_input_update(void)
{
    current_buttons = 0;

    // Read physical buttons (active low)
    bool boot_pressed = (gpio_get_level(PIN_BTN_BOOT) == 0);
    bool pwr_pressed = (gpio_get_level(PIN_BTN_PWR) == 0);

    // BOOT button: Coin + Start (with timing)
    // This implements Galagino's virtual coin/start mechanism
    if (boot_pressed) {
        current_buttons |= BTN_COIN;
    }

    // PWR button: For now, use as alternate directional
    // In the future, could map to IMU tilt
    if (pwr_pressed) {
        // Could be used for pause or special function
    }

    // TODO: Add IMU tilt control for directions
    // The FIESTA26 has a QMI8658 IMU that could provide
    // tilt-based joystick input for a more immersive experience

    // Virtual coin/start state machine
    static uint32_t last_time = 0;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    switch (virtual_coin_state) {
        case 0:  // Idle
            if (current_buttons & BTN_COIN) {
                virtual_coin_state = 1;
                virtual_coin_timer = now;
            }
            break;
        case 1:  // Coin pressed, wait 100ms
            if (now - virtual_coin_timer > 100) {
                virtual_coin_state = 2;
                virtual_coin_timer = now;
            }
            break;
        case 2:  // Coin released, wait 500ms
            if (now - virtual_coin_timer > 500) {
                virtual_coin_state = 3;
                virtual_coin_timer = now;
            }
            break;
        case 3:  // Start pressed, wait 100ms
            if (now - virtual_coin_timer > 100) {
                virtual_coin_state = 4;
                virtual_coin_timer = now;
            }
            break;
        case 4:  // Wait for button release
            if (!(current_buttons & BTN_COIN)) {
                virtual_coin_state = 0;
            }
            break;
    }
}

uint8_t pacman_read_in0(void)
{
    /*
     * IN0 port layout (directly from Galagino):
     *   bit 0: UP
     *   bit 1: LEFT
     *   bit 2: RIGHT
     *   bit 3: DOWN
     *   bit 4: unused
     *   bit 5: COIN
     *   bit 6: unused
     *   bit 7: unused
     *
     * Returns active-low (0 = pressed)
     */
    uint8_t retval = 0xFF;

    // Joystick directions
    if (current_buttons & BTN_UP)    retval &= ~0x01;
    if (current_buttons & BTN_LEFT)  retval &= ~0x02;
    if (current_buttons & BTN_RIGHT) retval &= ~0x04;
    if (current_buttons & BTN_DOWN)  retval &= ~0x08;

    // Coin (virtual state machine)
    if (virtual_coin_state == 1) {
        retval &= ~0x20;  // Coin active during state 1
    }

    return retval;
}

uint8_t pacman_read_in1(void)
{
    /*
     * IN1 port layout:
     *   bit 5: 1P START
     *
     * Returns active-low (0 = pressed)
     */
    uint8_t retval = 0xFF;

    // Start (virtual state machine)
    if (virtual_coin_state == 3 || virtual_coin_state == 4) {
        retval &= ~0x20;  // Start active during states 3-4
    }

    return retval;
}
