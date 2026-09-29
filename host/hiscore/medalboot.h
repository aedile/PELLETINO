/* medalboot.h, for the host: just the storage hiscore.c asks for, kept in memory */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
bool medalboot_load_blob(const char *rom, void *buf, size_t len);
void medalboot_save_blob(const char *rom, const void *buf, size_t len);
void medalboot_set_highscore(const char *rom, uint32_t score);
