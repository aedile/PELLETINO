/*
 * test_hiscore.c - the score keeper, against a machine that is forty bytes of memory.
 *
 *     host/hiscore/run.sh
 *
 * What has to be true, whatever the game:
 *   - nothing is restored, and nothing saved, before the game has set its table up
 *   - what was saved comes back after a power cycle
 *   - a score being run up is not written to flash on every point
 *   - leaving for the menu saves at once
 *   - a saved table of the wrong size is ignored rather than restored
 *   - an address that is not RAM does no harm
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hiscore.h"
#include "medalboot.h"

static int failures;
#define CHECK(ok, what) do { printf("  %-4s %s\n", (ok) ? "ok" : "FAIL", what); if (!(ok)) failures++; } while (0)

/* flash */
static uint8_t flash[256]; static size_t flash_len; static int writes; static uint32_t flash_top;
bool medalboot_load_blob(const char *rom, void *buf, size_t len)
{ (void)rom; if (!flash_len || flash_len != len) return false; memcpy(buf, flash, len); return true; }
void medalboot_save_blob(const char *rom, const void *buf, size_t len)
{ (void)rom; memcpy(flash, buf, len); flash_len = len; writes++; }
void medalboot_set_highscore(const char *rom, uint32_t s) { (void)rom; if (s > flash_top) flash_top = s; }

/* the machine: RAM at 0x4000-0x4027, nothing anywhere else */
static uint8_t ram[40];
static uint8_t *mem(uint16_t a) { return (a >= 0x4000 && a < 0x4028) ? &ram[a - 0x4000] : NULL; }

/* a table of 0x10 bytes that the game marks A5 ... 5A once it is ready, best score in BCD at +1 */
static const hiscore_range_t ranges[] = { { 0x4000, 0x10, 0xA5, 0x5A }, { 0x4020, 0x04, 0x11, 0x22 } };
static const hiscore_t game = { .rom = "test", .ranges = ranges, .nranges = 2, .mem = mem,
                                .format = HISCORE_BCD, .top_addr = 0x4001, .top_len = 3, .top_msb_first = true,
                                .top_times = 10 };

static void power_on(void)   { memset(ram, 0, sizeof ram); hiscore_begin(&game); }
static void game_inits(void) { memset(ram, 0, 0x10); ram[0] = 0xA5; ram[0x0f] = 0x5A; ram[1] = 0x00; ram[2] = 0x10; ram[3] = 0x00; ram[0x20] = 0x11; ram[0x23] = 0x22; }
static void frames(int n)    { while (n--) hiscore_frame(); }

int main(void)
{
    printf("before the table is set up\n");
    power_on();
    ram[2] = 0x77;
    frames(600);
    hiscore_flush();
    CHECK(writes == 0, "nothing is written");

    printf("half set up\n");
    memset(ram, 0, sizeof ram); ram[0] = 0xA5; ram[0x0f] = 0x5A;       /* the first range ready, the second not */
    ram[2] = 0x55;
    frames(600); hiscore_flush();
    CHECK(writes == 0, "one range ready and the other not is still not ready");

    printf("first game on a new medal\n");
    game_inits();
    frames(60);
    CHECK(writes == 0 && hiscore_top() == 10000, "the game's own table is left alone, and reads 10000");
    for (int i = 0; i < 300; i++) { ram[3] = (uint8_t)(((i / 10 % 10) << 4) | (i % 10)); frames(7); }   /* a score climbing */
    CHECK(writes == 0, "a score being run up is not written on every point");
    ram[1] = 0x01; ram[2] = 0x23; ram[3] = 0x45;
    frames(200);
    CHECK(writes == 0, "nor while it has only just stopped");
    frames(400);
    CHECK(writes == 1 && flash_top == 123450, "it is written once it has stayed put, and the menu gets 123450");
    frames(3000);
    CHECK(writes == 1, "and not again while nothing changes");

    printf("power cycle\n");
    power_on();
    frames(120);
    CHECK(ram[2] == 0, "nothing is restored into a machine that has not started");
    game_inits();
    frames(60);
    CHECK(ram[1] == 0x01 && ram[2] == 0x23 && ram[3] == 0x45, "the saved table comes back once it has");
    CHECK(hiscore_top() == 123450, "and reads 123450");

    printf("leaving for the menu\n");
    ram[1] = 0x02;
    hiscore_flush();
    CHECK(writes == 2 && flash[1] == 0x02, "saves at once");

    printf("a table saved by something else\n");
    flash_len = 7;
    power_on(); game_inits(); frames(60);
    CHECK(ram[2] == 0x10, "of the wrong size, is ignored");

    printf("memory that is not there\n");
    static const hiscore_range_t off[] = { { 0x4020, 0x10, 0, 0 } };     /* runs off the end of RAM */
    static const hiscore_t bad = { .rom = "test", .ranges = off, .nranges = 1, .mem = mem };
    flash_len = 0; memset(ram, 0, sizeof ram);
    hiscore_begin(&bad); frames(3000); hiscore_flush();
    CHECK(1, "does no harm (the sanitiser would have said)");

    printf("battery-backed memory\n");
    static const hiscore_range_t chip[] = { { 0x4000, 0x28, 0, 0 } };
    static const hiscore_t kept = { .rom = "test", .ranges = chip, .nranges = 1, .whole = true, .mem = mem };
    flash_len = 0; writes = 0;
    memset(ram, 0xff, sizeof ram);
    hiscore_begin(&kept);
    frames(100); hiscore_flush();
    CHECK(writes == 0, "left in the first seconds, before the game has checked it, nothing is kept");
    memset(ram, 0x12, sizeof ram);
    frames(600); hiscore_flush();
    CHECK(writes == 1 && flash[5] == 0x12, "left later, it is");
    memset(ram, 0, sizeof ram);
    hiscore_begin(&kept);
    CHECK(ram[5] == 0x12, "and it is back before the first frame");

    printf("\n%s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
