#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The roll, once through, drawn into the fest frame buffer the caller owns and
 * over whatever music is already playing. True if the button was pressed. */
bool credits_scene(void);

/* The carousel's Credits entry: the roll with a tune of its own, then back to
 * the menu and the menu's music. Releases and restores the menu's frame buffer. */
void credits_run(void);
#ifdef __cplusplus
}
#endif
