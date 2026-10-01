#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "starwars.h"

#ifdef __cplusplus
extern "C" {
#endif

void input_init(void);              /* buttons, battery enable, IMU */
void input_update(sw_input_t *in);  /* call once per loop; fills the game's input struct */
/* True while a person is plainly on the controls - the button, or a real tilt. The
 * autopilot yields to this at once. */
bool input_human_active(void);


#ifdef __cplusplus
}
#endif
