#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The opening, once through: about twelve seconds. Draws into the fest frame
 * buffer, which the caller owns, and leaves the music alone - so a tune can run
 * on from this into whatever follows. True if the button was pressed. */
bool splash_scene(void);
#ifdef __cplusplus
}
#endif
