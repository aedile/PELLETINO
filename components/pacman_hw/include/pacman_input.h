/*
 * pacman_input.h - Pac-Man Input Handling
 */

#ifndef PACMAN_INPUT_H
#define PACMAN_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Button bit definitions
#define BTN_UP      0x01
#define BTN_LEFT    0x02
#define BTN_RIGHT   0x04
#define BTN_DOWN    0x08
#define BTN_COIN    0x20
#define BTN_START   0x20

/**
 * Initialize input handling
 */
void pacman_input_init(void);

/**
 * Update input state (call once per frame)
 */
void pacman_input_update(void);

/**
 * Read IN0 port (joystick + coin)
 * @return Port value (active low)
 */
uint8_t pacman_read_in0(void);

/**
 * Read IN1 port (start buttons)
 * @return Port value (active low)
 */
uint8_t pacman_read_in1(void);

#ifdef __cplusplus
}
#endif

#endif // PACMAN_INPUT_H
