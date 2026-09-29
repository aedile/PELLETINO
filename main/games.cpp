/*
 * games.cpp - chain-booting a game image.
 *
 * Every game gets an app partition LABELLED WITH ITS ROM NAME (see partitions.csv),
 * so the launcher needs no table mapping games to slots: it asks flash whether a
 * partition by that name exists. A game that has never been flashed simply has no
 * partition, and the menu greys it out. Adding a game means adding a marquee and a
 * partition - the launcher never gets recompiled.
 *
 * A partition can exist and still be empty: the table is generated from the ROMs
 * present, so a game whose firmware failed to build (or was never flashed) has a
 * slot full of 0xFF. "Installed" therefore means the slot exists AND starts with
 * an app image header; esp_ota_set_boot_partition() would refuse the empty one
 * anyway, and the menu should say NOT INSTALLED rather than HOLD TO PLAY.
 *
 * Which game is selected, and the handshake every game owes the menu, live in
 * components/medalboot - shared so the games can use the same API.
 */
#include "games.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "games";

static const esp_partition_t *find_game(const char *rom)
{
    if (!rom || !*rom) return NULL;
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, rom);
}

/* Does the slot hold an image? One flash read per slot, remembered: the menu asks
 * every frame and flash does not change under a running launcher. */
static bool slot_has_image(const esp_partition_t *p)
{
    enum { SLOTS = 16 };
    static const esp_partition_t *seen[SLOTS];
    static bool has[SLOTS];
    static int n;

    for (int i = 0; i < n; i++)
        if (seen[i] == p) return has[i];

    uint8_t magic = 0xFF;
    bool ok = esp_partition_read(p, 0, &magic, 1) == ESP_OK && magic == ESP_IMAGE_HEADER_MAGIC;
    if (n < SLOTS) { seen[n] = p; has[n] = ok; n++; }
    return ok;
}

bool game_installed(const char *rom)
{
    const esp_partition_t *p = find_game(rom);
    return p && slot_has_image(p);
}

bool game_launch(const char *rom)
{
    const esp_partition_t *p = find_game(rom);
    if (!p) {
        ESP_LOGW(TAG, "%s is not installed", rom);
        return false;
    }

    /* Verifies the image first: an empty or corrupt slot is refused here, not
     * discovered by the bootloader after the restart. */
    esp_err_t err = esp_ota_set_boot_partition(p);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not select %s: %s", rom, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "booting %s at 0x%06x", rom, (unsigned)p->address);
    esp_restart();
    return true;   /* unreachable */
}
