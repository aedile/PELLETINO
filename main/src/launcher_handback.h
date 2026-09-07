/*
 * launcher_handback.h - point the boot partition back at the MINIMAME launcher.
 *
 * The menu medal chain-boots a game by setting the boot partition and restarting. If the game
 * left it pointed at itself, then a crash, a watchdog bite or a brownout would boot straight
 * back into the same game and could loop there forever. So the first thing a game does, before
 * any initialisation that might go wrong, is aim the boot partition at the launcher again.
 * From then on every exit path - a clean restart, a crash, a power glitch - lands in the menu.
 * The cost is one flash write per launch.
 *
 * A standalone build has no "launcher" partition, so this does nothing there.
 */
#pragma once
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"

static inline void launcher_handback(void)
{
    const esp_partition_t *l = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "launcher");
    if (l && esp_ota_set_boot_partition(l) == ESP_OK)
        ESP_LOGI("BOOT", "boot partition returned to the launcher");
}
