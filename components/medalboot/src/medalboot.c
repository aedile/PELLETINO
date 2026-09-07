#include "medalboot.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "medalboot";

#define NS          "minimame"
#define K_SELECTED  "selected"   /* rom to auto-boot; absent = show the menu */
#define K_LAST      "last"       /* rom the menu should open on */
#define K_ATTEMPTS  "attempts"   /* consecutive unconfirmed boots of K_SELECTED */

static bool get_str(const char *key, char *out, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = len;
    esp_err_t err = nvs_get_str(h, key, out, &n);
    nvs_close(h);
    return err == ESP_OK && out[0] != '\0';
}

static void set_str(const char *key, const char *val)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (val) nvs_set_str(h, key, val);
    else     nvs_erase_key(h, key);
    nvs_commit(h);
    nvs_close(h);
}

static void set_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, v);
    nvs_commit(h);
    nvs_close(h);
}

bool medalboot_get_selected(char *out, size_t len) { return get_str(K_SELECTED, out, len); }
bool medalboot_get_last(char *out, size_t len)     { return get_str(K_LAST, out, len); }
bool medalboot_rom(char *out, size_t len)          { return get_str(K_SELECTED, out, len); }

void medalboot_set_selected(const char *rom)
{
    set_str(K_SELECTED, rom);
    set_str(K_LAST, rom);
    set_u8(K_ATTEMPTS, 0);
}

void medalboot_clear_selected(void)
{
    set_str(K_SELECTED, NULL);
    set_u8(K_ATTEMPTS, 0);
}

int medalboot_attempts(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    uint8_t v = 0;
    nvs_get_u8(h, K_ATTEMPTS, &v);
    nvs_close(h);
    return v;
}

void medalboot_note_attempt(void)
{
    int v = medalboot_attempts();
    if (v < 255) set_u8(K_ATTEMPTS, (uint8_t)(v + 1));
}

void medalboot_game_startup(void)
{
    const esp_partition_t *l = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "launcher");
    if (l) {
        esp_err_t err = esp_ota_set_boot_partition(l);
        if (err != ESP_OK) ESP_LOGW(TAG, "could not arm the menu: %s", esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "no 'launcher' partition - this image cannot return to the menu");
    }
}

void medalboot_game_running(void) { set_u8(K_ATTEMPTS, 0); }

bool medalboot_exit_hold(bool down)
{
    static int64_t since;
    static bool was_down, fired;
    int64_t now = esp_timer_get_time();

    if (down && !was_down) { since = now; fired = false; }
    was_down = down;
    if (!down) return false;

    if (!fired && now - since >= (int64_t)MEDALBOOT_EXIT_HOLD_MS * 1000) {
        fired = true;
        return true;
    }
    return false;
}

void medalboot_exit_to_menu(void)
{
    ESP_LOGI(TAG, "returning to the menu");
    medalboot_clear_selected();
    esp_restart();
}
