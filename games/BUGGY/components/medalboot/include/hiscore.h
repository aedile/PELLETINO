/*
 * hiscore.h - a game's high scores, kept across power cycles.
 *
 * The cabinets mostly forgot their scores when they were switched off, and the
 * ones that did not kept them in a chip of their own. Either way the scores
 * are a few dozen bytes somewhere in the machine's memory, and where is known:
 * MAME's hiscore.dat lists the addresses for thousands of games, along with the
 * two values that show the game has finished setting its table up. This does
 * what MAME's plugin does with that list.
 *
 *   wait    until the first and last byte of every range hold the values the
 *           game itself puts there, so nothing is restored over a table the
 *           game is about to initialise. Those values are how a fresh table
 *           is recognised and nothing more: a table with scores in it no
 *           longer has them, and is saved all the same
 *   restore what was saved, if anything was
 *   watch   and save again once the bytes have changed and then stayed put for
 *           a few seconds - a score being run up is not written to flash on
 *           every point - and on the way out to the menu or to power off
 *
 * A range marked `whole` is a chip the cabinet kept powered: it is put back
 * before the game starts, without waiting, and saved only on the way out.
 *
 * The game says where its memory is; this knows nothing about any machine.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t addr, len;         /* in the game's own address space */
    uint8_t  first, last;       /* what the first and last byte hold once the table is set up */
} hiscore_range_t;

/* how the best score reads, for the number the menu shows */
typedef enum {
    HISCORE_NO_NUMBER = 0,      /* saved and restored, but the menu shows no figure */
    HISCORE_BCD,                /* two digits a byte */
    HISCORE_DIGITS,             /* one digit a byte, as the character drawn for it */
    HISCORE_BINARY,             /* a plain number */
} hiscore_format_t;

typedef struct {
    const char            *rom;         /* the key it is saved under */
    const hiscore_range_t *ranges;
    int                    nranges;
    bool                   whole;       /* battery-backed memory: restore at once, save on the way out */
    uint16_t               wait_frames; /* look for the table only after this many frames: for a game
                                         * whose fresh table cannot be told from memory it has yet to clear */
    uint8_t             *(*mem)(uint16_t addr);   /* the byte at this address, or NULL if it is not RAM */

    /* the best score, for the menu */
    hiscore_format_t       format;
    uint16_t               top_addr;    /* its first byte */
    uint8_t                top_len;     /* bytes */
    bool                   top_msb_first;   /* most significant byte at top_addr */
    uint8_t                digit_zero;  /* HISCORE_DIGITS: the character that is '0' */
    uint8_t                digit_blank; /* and the one drawn for a leading blank */
    uint32_t               top_times;   /* multiply by this (a game that keeps tens of points); 0 is 1 */
    uint32_t             (*top)(void);  /* or work it out yourself, if none of the above describes it */
} hiscore_t;

void hiscore_begin(const hiscore_t *game);  /* once, after the machine is initialised */
void hiscore_frame(void);                   /* once for every frame emulated */
void hiscore_flush(void);                   /* save now if there is anything to save */

uint32_t hiscore_top(void);                 /* the best score as it stands, 0 if it cannot be read */

#ifdef __cplusplus
}
#endif
