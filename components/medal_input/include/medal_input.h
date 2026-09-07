/*
 * medal_input.h - the part of a Fiesta medal's controls that every game shares.
 *
 * Each medal is a Waveshare ESP32-C6-LCD-1.69 in a pin: an ST7789 panel, a QMI8658 IMU, two
 * buttons (BOOT and PWR) and a battery rail that the firmware has to hold on. Every game does
 * the same things with that hardware - hold the rail up, watch the two buttons, run the
 * coin-then-start sequence off a short press, power down on a long one, toggle the sound on a
 * long press of the other button, and turn the accelerometer into an angle relative to however
 * the player is holding the thing.
 *
 * What differs between games is only the last step: what the angle *means*. A trackball wants a
 * rate, a four-way stick wants a direction with hysteresis, a spinner wants an absolute
 * position, a yoke wants two axes of it. That part stays in each game. Everything above it
 * lives here, so a fix lands once instead of eleven times.
 *
 * ---- the tilt model ----
 *
 * A medal is held upright, like a phone, so gravity lies in the plane of the panel. Two angles
 * come out of that:
 *
 *   lr  atan2(ay, ax) - the direction of gravity *within* the panel's plane. Rotating the medal
 *       in your fingers - a twist - sweeps this through 360 degrees. This is the axis a spinner,
 *       a steering wheel and a left/right stick all use.
 *   ud  atan2(az, |xy|) - how far gravity leaves that plane. Tipping the top edge toward you or
 *       away from you sweeps this. This is up/down.
 *
 * Both are reported relative to a captured neutral pose, so "however I am holding it right now"
 * is centre.
 *
 * ---- why the pose has to be checked ----
 *
 * Those angles only mean anything while the medal is actually being held up. Lying flat on a
 * desk, gravity points straight out of the screen, the in-plane component is near zero and its
 * atan2 is noise. A neutral pose captured there is a centre the player was never holding: on a
 * threshold control it is merely wrong, but on an absolute one - Arkanoid's paddle - it pins the
 * control against one end of its travel, so one direction does nothing at all and the other
 * sweeps the whole range the moment it crosses back into range.
 *
 * So nothing is captured or reported until gravity is genuinely in the plane. The zero is taken
 * at the first such moment, again on every coin, and again when start goes in half a second
 * later - by which point the player has settled into playing posture.
 */
#ifndef MEDAL_INPUT_H
#define MEDAL_INPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads the raw accelerometer. This is qmi8658_read_accel in every medal so far; it is a
 * pointer rather than a direct call so this component depends on no particular IMU driver. */
typedef void (*medal_read_accel_fn)(int16_t *x, int16_t *y, int16_t *z);

typedef struct {
    /* The IMU. init_i2c brings up I2C0 on GPIO 8/7 first, then imu_init is called, and
     * read_accel is used from then on - so the game hands over qmi8658_init and
     * qmi8658_read_accel and does not have to get the ordering right itself. */
    bool     init_i2c;
    bool   (*imu_init)(void);
    medal_read_accel_fn read_accel;   /* NULL means no tilt at all */

    uint32_t imu_period_us;           /* 0 -> 16000. A paddle wants nearer 5000. */
    uint32_t power_off_hold_us;       /* 0 -> 1000000 */
    uint32_t mute_hold_us;            /* 0 disables the gesture entirely */
    uint32_t coin_us, gap_us, start_us;  /* 0 -> 100000 / 400000 / 100000 */

    /* Set when PWR means something else in this game. The rail and the power-off hold are still
     * handled here; coin and start are not reported, and the game reads pwr_released and
     * pwr_release_held_us to decide for itself. Pole Position needs this: its PWR has three
     * bands - a tap is a coin, a medium press changes gear, a long one powers off. */
    bool     manual_pwr;

    void (*on_mute)(void);            /* BOOT held for mute_hold_us */
    void (*on_recentre)(void);        /* the neutral pose was just (re)captured; clear latched
                                       * per-game state - accumulated counts, a sticky axis, a
                                       * filtered paddle position - so it starts from centre */
} medal_input_config_t;

typedef struct {
    bool boot, pwr;                   /* the two buttons, true = pressed */
    bool coin, start;                 /* driven by the coin-then-start sequence */

    bool tilt_valid;                  /* lr/ud below can be trusted */
    bool tilt_fresh;                  /* a new IMU sample landed on this poll */
    float lr, ud;                     /* degrees relative to the neutral pose, both wrapped */

    /* raw button timing, for gestures a game wants to add of its own */
    int64_t boot_held_us;             /* how long BOOT has been down; 0 when up */
    int64_t pwr_held_us;
    bool    boot_released;            /* BOOT came up on this poll */
    int64_t boot_release_held_us;     /* and this is how long it had been down */
    bool    pwr_released;
    int64_t pwr_release_held_us;
} medal_input_state_t;

void medal_input_init(const medal_input_config_t *cfg);
void medal_input_poll(medal_input_state_t *st);

/* Start the coin-then-start sequence from somewhere other than a PWR press. Pac-Man has no
 * fire button, so its coin has always been on BOOT; this lets that keep working while PWR does
 * the same thing it does on every other medal. Ignored if a sequence is already running. */
void medal_input_insert_coin(void);

/* Ask for the zero to be taken again. Returns false if the medal is not being held up, in
 * which case the old zero is kept and the next poll tries again. */
bool medal_input_recentre(void);
bool medal_input_have_neutral(void);
bool medal_input_imu_ok(void);

/* Cut the battery rail. medal_input_poll() does this itself on a long press. */
void medal_input_power_off(void);

/* Wrap a difference of two angles into -180..180. Exposed because per-game mapping code
 * generally needs it too. */
float medal_wrap_deg(float d);

#ifdef __cplusplus
}
#endif
#endif
