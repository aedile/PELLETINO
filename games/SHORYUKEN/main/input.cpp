/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro */
/*
 * input.cpp - the medal's controls, as far as a bench needs them.
 *
 * The buttons, the power rail, the coin-then-start sequence, the sound gesture and the hold
 * that leaves for the menu all live in components/medal_input, which every game shares, and
 * they behave here as they do everywhere else. What is this game's own is small, because
 * this is a measuring instrument and not a game: a coin and a start so that it can be seen
 * to take them, and a jab. The accelerometer is not read at all.
 *
 *   BOOT                -> jab punch; hold 5 s -> back to the menu
 *   PWR short press     -> coin, then start half a second later; hold 1 s -> power off
 *   both together       -> sound: loud, quiet, off
 */
#include "input.h"
#include "medal_input.h"
#include "display.h"
#include "medalboot.h"
#include "audio_hal.h"
#include "knobs.h"
#include "esp_log.h"

static const char *TAG = "INPUT";

static void on_mute(void)
{
    medalboot_sound_t s = medalboot_sound_next();      /* loud, quiet, off - and it holds everywhere */
#if SOUND != SOUND_OFF
    audio_set_volume(medalboot_sound_volume(s));
    audio_set_mute(s == MEDALBOOT_SOUND_OFF);
#endif
    display_toast(medalboot_sound_name(s), 1500);
    ESP_LOGI(TAG, "%s", medalboot_sound_name(s));
}

void input_init(void)
{
    medal_input_config_t cfg = {};
    /* no accelerometer: nothing here is steered, and reading it cost 84 ms of every second.
     * The I2C bus is still brought up, because the sound codec is on it. */
    cfg.init_i2c = true;
    cfg.read_accel = nullptr;
    cfg.mute_hold_us = 3000000;
    cfg.on_mute = on_mute;
    cfg.exit_hold_us = MEDALBOOT_EXIT_HOLD_MS * 1000;   /* hold to leave for the menu */
    cfg.on_exit = medalboot_exit_to_menu;
    medal_input_init(&cfg);
}

void input_update(cps1_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    in->coin1 = st.coin ? 1 : 0;
    in->start1 = st.start ? 1 : 0;
    in->p1_b1 = st.boot ? 1 : 0;
}
