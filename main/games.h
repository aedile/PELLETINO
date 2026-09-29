#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* A game is installed iff an app partition labelled with its ROM name exists AND
 * holds an app image (a generated slot can exist with nothing flashed into it). */
bool game_installed(const char *rom);
/* Chain-boots the game. Does not return on success; false if the slot is empty or
 * the image does not verify, in which case nothing has been changed. */
bool game_launch(const char *rom);
#ifdef __cplusplus
}
#endif
