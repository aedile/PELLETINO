#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Loud, quiet or off, for the whole device. The setting is saved, and every
 * game reads the same one - see medalboot_sound(). */
void sound_init(void);          /* after audio_init(): applies the saved setting */
bool sound_muted(void);
bool sound_quiet(void);
bool sound_poll(void);          /* call every loop; true if the player just changed it */
#ifdef __cplusplus
}
#endif
