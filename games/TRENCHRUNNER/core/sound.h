/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * sound.h - Atari Star Wars sound board
 *
 * MC6809E @ 1.512 MHz, MOS 6532 RIOT (RAM, timer, PA7 command interrupt),
 * four POKEYs (music/effects), TMS5220 speech. Commands arrive from the main
 * CPU through an 8-bit latch; replies go back through another.
 */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* rom: 16KB = 136021.107 (CPU 0x4000/0xC000) followed by 136021.208 (0x6000/0xE000) */
void snd_init(const uint8_t *rom);
void snd_reset(void);            /* power-on reset of the whole board */
void snd_cpu_reset(void);        /* main CPU's SOUNDRST: resets the sound CPU only */
uint32_t snd_run(uint32_t cycles);

/* main CPU side of the latches */
void snd_command_write(uint8_t d);      /* main writes 0x4400 */
uint8_t snd_reply_read(void);           /* main reads 0x4400 */
uint8_t snd_ready_flags(void);          /* main reads 0x4401: bit7 = command pending, bit6 = reply pending */

/* audio output: mixes the four POKEYs (and speech) into signed 16-bit mono */
void snd_render(int16_t *buf, int samples, int sample_rate);
uint32_t snd_speech_underruns(void);
void snd_speech_debug(uint32_t *written, uint32_t *rd, uint32_t *calls, uint32_t *rate, uint32_t *talkd);   /* diagnostic: speech ring read while empty mid-phrase */

/* diagnostics */
uint16_t snd_pc(void);
uint32_t snd_irq_count(void);
uint32_t snd_pokey_writes(void);
uint32_t snd_commands(void);
uint64_t snd_total_cycles(void);
uint32_t snd_idle_skipped(void);        /* cycles skipped in the idle loop since last call */
#ifdef SW_DEBUG
extern uint32_t snd_dbg_pc_hist[0x10000];
extern int snd_dbg_log_pokey, snd_dbg_log_tms;
void snd_dbg_dump(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
