#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The instructions, as part of attract mode. Draws into the frame buffer the
 * caller owns; true if a button ended it. */
bool howto_scene(void);
#ifdef __cplusplus
}
#endif
