/*
 * preview.c - render the launcher's wheel on a desktop, at the real 240x280, using the real
 * renderer out of main/menu.cpp and the real artwork blob. Writes PPMs.
 *
 * Nothing here reimplements the layout: menu.cpp, components/fest and components/mqart are
 * compiled as they are, and the only things stubbed are the flash partition, the panel, the
 * buttons and the battery.
 *
 * It is also the check on the artwork reader: it is run a second time over a blob with
 * every picture's bytes scrambled, under the address sanitiser, and has to survive it.
 */
#include "stubs.h"
#include "fest.h"
#include "mqart.h"
#include "menu.h"
#include "display.h"
#include "input.h"
#include "games.h"
#include "battery.h"
#include "sound.h"
#include "chiptune.h"

/* --- the artwork blob, out of a file rather than a partition --- */
static uint8_t *blob;
static esp_partition_t blob_part;

const esp_partition_t *esp_partition_find_first(esp_partition_type_t t, esp_partition_subtype_t s, const char *label)
{
    (void)t; (void)s; (void)label;
    return blob ? &blob_part : NULL;
}

esp_err_t esp_partition_mmap(const esp_partition_t *p, size_t off, size_t n, int kind,
                             const void **out, esp_partition_mmap_handle_t *h)
{
    (void)p; (void)n; (void)kind;
    *out = blob + off; *h = 0;
    return ESP_OK;
}

/* --- the panel: collect the strips into one image --- */
static uint16_t screen[FB_H][FB_W];
static int win_y, win_row;

void display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    (void)x; (void)w; (void)h;
    win_y = y; win_row = 0;
}

void display_write_preswapped(const uint16_t *px, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        int row = win_y + win_row + (int)(i / FB_W), col = (int)(i % FB_W);
        if (row >= 0 && row < FB_H) screen[row][col] = px[i];
    }
    win_row += (int)(n / FB_W);
}

void display_wait_done(void) { }

/* --- everything else the wheel asks about --- */
static int hold_ms, all_installed = 1, muted, quiet, charge = 82;
int  input_hold_ms(void)            { return hold_ms; }
bool game_installed(const char *r)  { (void)r; return all_installed; }
int  battery_percent(void)          { return charge; }
bool sound_muted(void)              { return muted; }
bool sound_quiet(void)              { return quiet; }
void chip_tone(int hz)              { (void)hz; }
uint32_t medalboot_get_highscore(const char *rom) { return !strcmp(rom, "galaga") ? 30000 : 0; }
void chip_sfx(chip_sfx_t w)         { (void)w; }

static const char *outdir;
static int written;

static void shot(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) {
            uint16_t be = screen[y][x];
            uint16_t c = (uint16_t)((be >> 8) | (be << 8));   /* the panel's byte order */
            uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 0x1f) << 3),
                               (uint8_t)(((c >> 5) & 0x3f) << 2),
                               (uint8_t)((c & 0x1f) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    written++;
}

static void settle(void) { for (int i = 0; i < 40; i++) menu_render(); }

int main(int argc, char **argv)
{
    const char *blobpath = argc > 1 ? argv[1] : "../lcd/marquees.bin";
    outdir = argc > 2 ? argv[2] : "/tmp";
    int scramble = argc > 3 && !strcmp(argv[3], "--scramble");

    FILE *f = fopen(blobpath, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", blobpath); return 1; }
    fseek(f, 0, SEEK_END); blob_part.size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    blob = malloc(blob_part.size);                  /* exactly: the sanitiser guards the end */
    if (fread(blob, 1, blob_part.size, f) != blob_part.size) return 1;
    fclose(f);

    if (scramble) {
        /* leave the names alone and wreck the rest: sizes, offsets, row tables, pixels */
        int n = blob[4] | (blob[5] << 8), esz = blob[6] | (blob[7] << 8);
        srand(1);
        for (int i = 0; i < n; i++)
            for (int k = 72; k < esz; k++)
                if (rand() % 3 == 0) blob[8 + i * esz + k] = (uint8_t)rand();
        for (size_t i = 8 + (size_t)n * esz; i < blob_part.size; i++)
            if (rand() % 4 == 0) blob[i] = (uint8_t)rand();
    }

    if (mqart_init() != ESP_OK) { fprintf(stderr, "mqart_init failed\n"); return 1; }
    menu_init();
    menu_set_mode(MENU_BROWSE);

    char name[64];
    int n = mqart_count();
    for (int i = 0; i < n; i++) {                   /* every game, at rest */
        menu_select_rom(mqart_get(i)->rom);
        settle();
        snprintf(name, sizeof name, "wheel_%02d_%s", i, mqart_get(i)->rom);
        shot(name);
    }

    menu_select_rom(mqart_get(0)->rom);             /* one step, frame by frame */
    settle();
    menu_nav(+1);
    for (int i = 0; i < 12; i++) {
        menu_render();
        snprintf(name, sizeof name, "step_%02d", i);
        shot(name);
    }
    menu_nav(-1); menu_nav(-1); menu_nav(-1);       /* and more taps than it will queue */
    menu_render(); shot("step_back_piled_up");
    settle();

    all_installed = 0; menu_render(); shot("state_absent");   all_installed = 1;
    hold_ms = 900;     menu_render(); shot("state_hold");     hold_ms = 0;
    muted = 1; charge = 4; menu_render(); shot("state_muted_flat"); muted = 0; charge = 82;
    quiet = 1; hold_ms = 1900; menu_render(); shot("state_quiet_hold_nearly_full"); quiet = 0; hold_ms = 0;
    menu_set_mode(MENU_SHOWCASE); menu_nav(+1); settle(); shot("state_showcase");
    menu_set_mode(MENU_LAUNCHING); menu_render(); shot("state_launching");
    menu_set_mode(MENU_BROWSE); menu_begin_launch();
    for (int i = 0; !menu_launch_done(); i++) {
        menu_render();
        snprintf(name, sizeof name, "launch_%02d", i);
        shot(name);
    }
    menu_set_mode(MENU_BROWSE);
    menu_show_message("COULD NOT START", "HOLD TO PICK ANOTHER"); menu_render(); shot("state_message");

    printf("wrote %d screens to %s%s\n", written, outdir, scramble ? " (scrambled artwork)" : "");
    return 0;
}
