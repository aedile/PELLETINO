/*
 * starwars.h - Atari Star Wars (1983) main board emulation
 *
 * Main CPU: MC6809E @ 1.512 MHz (12.096 MHz / 8), IRQ at 12.096 MHz / 4096 / 12 = 246 Hz.
 * Memory map, matrix processor ("mathbox"), divider, PRNG, banking, inputs,
 * and the AVG hookup follow MAME's starwars.cpp / starwars_m.cpp (BSD-3-Clause).
 */
#ifndef STARWARS_H
#define STARWARS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "avg.h"

#define SW_MASTER_CLOCK   12096000
#define SW_CPU_CLOCK      (SW_MASTER_CLOCK / 8)          /* 1512000 */
#define SW_IRQ_HZ         (SW_MASTER_CLOCK / 4096 / 12)  /* 246 */
#define SW_CPU_CYCLES_PER_IRQ (SW_CPU_CLOCK / SW_IRQ_HZ)  /* 6146 */

typedef struct {
    /* program ROM images (caller owns the memory) */
    const uint8_t *rom_main;      /* 32KB: CPU 0x8000-0xFFFF (102,203,104,206) */
    const uint8_t *rom_bank;      /* 16KB: 136021.214, two 8KB pages for 0x6000-0x7FFF */
    const uint8_t *rom_vector;    /* 4KB: 136021.105, CPU 0x3000-0x3FFF */
    const uint8_t *prom_mathbox;  /* 4KB: 110,111,112,113 concatenated */
    const uint8_t *prom_avg;      /* 256B: 136021-105.1l */
} sw_roms_t;

typedef struct {
    uint8_t pitch;    /* 0..255, 0x80 = center (ADC channel 0) */
    uint8_t yaw;      /* 0..255, 0x80 = center (ADC channel 1) */
    uint8_t coin1;    /* 1 = pressed */
    uint8_t coin2;
    uint8_t fire;     /* BUTTON1 (IN0 bit7) */
    uint8_t button2;  /* IN1 bit5 */
    uint8_t button3;  /* IN1 bit4 */
    uint8_t button4;  /* IN0 bit6 */
    uint8_t service;  /* self-test switch */
} sw_input_t;

void sw_init(const sw_roms_t *roms);
void sw_reset(void);

/* Run the main CPU for approximately `cycles` CPU cycles (1.512 MHz). Returns cycles run. */
uint32_t sw_run(uint32_t cycles);

/* Attach the sound board (16KB: 136021.107 then 136021.208). Without it the
 * main CPU sees a silent, always-ready sound board. */
void sw_attach_sound(const uint8_t *rom_sound);

/* Inputs are sampled by the game code; update this struct any time. */
sw_input_t *sw_input(void);

/* DIP switches (active-low bits as read by the game). Defaults: DSW0 0x98, DSW1 0x02 (1C/1C). */
void sw_set_dips(uint8_t dsw0, uint8_t dsw1);

/* Called whenever the game starts a vector list (VGGO). The list is complete when called. */
typedef void (*sw_frame_cb_t)(const avg_t *avg, void *user);
void sw_set_frame_callback(sw_frame_cb_t cb, void *user);

/* Optional profiling: microsecond time source (NULL = disabled) and accumulated costs */
typedef struct { uint64_t avg_us, math_us, frame_cb_us; uint32_t avg_steps, math_steps; } sw_stats_t;
void sw_set_time_source(uint64_t (*now_us)(void));
sw_stats_t *sw_stats(void);

uint32_t sw_idle_skipped(void);
uint32_t sw_soundrst_count(void);     /* SOUNDRST writes by the main CPU (1 at boot is normal) */       /* main CPU cycles skipped in wait loops since last call */

/* Diagnostics */
uint64_t sw_total_cycles(void);
uint32_t sw_irq_count(void);
uint32_t sw_frame_count(void);
uint16_t sw_pc(void);
uint8_t  sw_nvram_read(int idx);
uint8_t *sw_mem(uint16_t addr);      /* a byte of the non-volatile RAM by its CPU address, or NULL */
const uint8_t *sw_ram(void);          /* 0x0000-0x2FFF vector RAM */

#ifdef __cplusplus
}
#endif

#endif
