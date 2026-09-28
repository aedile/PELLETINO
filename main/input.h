#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Hold the button this long on a game to pick it. Short taps do nothing, so the
 * medal cannot be started by a knock in a pocket. */
#define INPUT_SELECT_HOLD_MS 2000

typedef enum { NAV_NONE = 0, NAV_PREV, NAV_NEXT } nav_t;

void  input_init(void);
void  input_poll(void);          /* call every loop */
nav_t input_take_nav(void);      /* one step per detent, or NAV_NONE */
bool  input_imu_ok(void);

bool  input_button_down(void);   /* live level, usable before the loop starts */
int   input_hold_ms(void);       /* how long it has been held, 0 when released */
bool  input_take_hold(void);     /* fires once, when the hold reaches the threshold */
bool  input_take_mute(void);     /* both buttons pressed together */
bool  input_take_wake(void);     /* either button pressed and let go, on its own */

#ifdef __cplusplus
}
#endif
