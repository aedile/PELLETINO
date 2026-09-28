/*
 * apu2a03.h - the NES sound chip (Ricoh 2A03 APU): two pulse channels, a
 * triangle, noise and the delta-modulation channel.
 *
 * Written for PELLETINO from the chip's published behaviour. It is here so the
 * launcher can play NSF music without carrying an NES emulator - and in
 * particular without nofrendo, which is GPL and would relicense the launcher.
 *
 * Unlike ay_render(), apu_render() WRITES its buffer rather than adding to it.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t (*apu_read_fn)(uint16_t addr);      /* the DMC fetches its samples from memory */

typedef struct {
    uint8_t  duty, halt, constant, vol;
    uint8_t  env_start, env_div, env_decay;
    uint8_t  sweep_en, sweep_period, sweep_neg, sweep_shift, sweep_reload, sweep_div;
    uint16_t period;
    int32_t  timer;                                  /* CPU cycles until the sequencer steps */
    uint8_t  seq, length, enabled;
} apu_pulse_t;

typedef struct {
    uint8_t  control, reload_val, linear, reload;
    uint16_t period;
    int32_t  timer;
    uint8_t  seq, length, enabled;
} apu_tri_t;

typedef struct {
    uint8_t  halt, constant, vol, env_start, env_div, env_decay;
    uint8_t  mode, period_idx, length, enabled;
    uint16_t lfsr;
    int32_t  timer;
} apu_noise_t;

typedef struct {
    uint8_t  loop, rate_idx, level, addr_reg, len_reg;
    uint8_t  buffer, buffer_full, shift, bits, silence;
    uint16_t addr, remaining;
    int32_t  timer;
} apu_dmc_t;

typedef struct {
    apu_pulse_t pulse[2];
    apu_tri_t   tri;
    apu_noise_t noise;
    apu_dmc_t   dmc;
    uint8_t     frame_mode, frame_step;
    int32_t     frame_timer;
    uint32_t    acc;                                 /* 16.16 CPU cycles carried between samples */
    int32_t     dc;
    apu_read_fn read;
} apu_t;

void    apu_reset(apu_t *a, apu_read_fn read);
void    apu_write(apu_t *a, uint16_t addr, uint8_t v);   /* 0x4000-0x4017 */
uint8_t apu_status(const apu_t *a);                      /* a read of 0x4015 */
void    apu_render(apu_t *a, int16_t *buf, int samples, int rate);

#ifdef __cplusplus
}
#endif
