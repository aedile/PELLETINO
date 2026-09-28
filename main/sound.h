#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Sound on or off, for the whole medal. The setting is saved, and every game
 * reads the same one - see medalboot_muted(). */
void sound_init(void);          /* after audio_init(): applies the saved setting */
bool sound_muted(void);
bool sound_poll(void);          /* call every loop; true if the player just changed it */
#ifdef __cplusplus
}
#endif
