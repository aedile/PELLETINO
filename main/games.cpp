/*
 * games.cpp - chain-booting a game image.
 *
 * Every game gets an app partition LABELLED WITH ITS ROM NAME (see partitions.csv),
 * so the launcher needs no table mapping games to slots: it asks flash whether a
 * partition by that name exists. A game that has never been flashed simply has no
 * partition, and the menu greys it out. Adding a game means adding a marquee and a
 * partition - the launcher never gets recompiled.
 *
 * Which game is selected, and the handshake every game owes the menu, live in
 * components/medalboot - shared so the games can use the same API.
 */
#include "games.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "games";

static const esp_partition_t *find_game(const char *rom)
{
    if (!rom || !*rom) return NULL;
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, rom);
}

/* A slot that exists is not a game: the partition table lays every slot out before
 * anything is flashed into it, and an empty one reads as blank. So installed means
 * the slot holds an application too - the descriptor every image starts with. */
bool game_installed(const char *rom)
{
    const esp_partition_t *p = find_game(rom);
    esp_app_desc_t desc;
    return p && esp_ota_get_partition_description(p, &desc) == ESP_OK;
}

bool game_launch(const char *rom)
{
    const esp_partition_t *p = find_game(rom);
    if (!p || !game_installed(rom)) {
        ESP_LOGW(TAG, "%s is not installed", rom);
        return false;
    }

    esp_err_t err = esp_ota_set_boot_partition(p);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not select %s: %s", rom, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "booting %s at 0x%06x", rom, (unsigned)p->address);
    esp_restart();
    return true;   /* unreachable */
}
