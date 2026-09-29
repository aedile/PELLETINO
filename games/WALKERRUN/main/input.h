#pragma once
#include <stdint.h>
#include "starwars.h"

#ifdef __cplusplus
extern "C" {
#endif

void input_init(void);              /* buttons, battery enable, IMU */
void input_update(sw_input_t *in);  /* call once per loop; fills the game's input struct */

/* Long holds of the fire button, reported once each (see input.cpp) */
enum { GESTURE_NONE = 0, GESTURE_TOGGLE_SOUND };
int input_take_gesture(void);

#ifdef __cplusplus
}
#endif
