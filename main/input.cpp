/*
 * input.cpp - menu controls for the PELLETINO launcher.
 *   BOOT button (GPIO9)  -> short press: next game; HOLD to pick it (INPUT_SELECT_HOLD_MS)
 *   PWR button (GPIO18)  -> short press: previous game; long (1 s): power off
 *   both together        -> sound: loud, quiet, off
 *   nothing, for a while -> the backlight goes down, then off; a button or being moved
 *                           brings it back
 *   tilt (QMI8658)       -> off for now. Four passes of tuning never made it feel right in
 *                           the hand, so the buttons drive the carousel until it does; the
 *                           detent code is kept below, behind NAV_TILT.
 *
 * The detent is the important part. Raw tilt would rip through the whole carousel in half a
 * second, so a step only fires when roll crosses NAV_ON_DEG, and no further step can fire
 * until roll falls back inside NAV_OFF_DEG. Holding past the threshold auto-repeats, and the
 * repeat winds up the longer it is held - see the constants below.
 */
#include "input.h"
#include "qmi8658.h"
#include "battery.h"
#include "display.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdlib.h>

static const char *TAG = "input";

#define PIN_BTN_BOOT      GPIO_NUM_9
#define PIN_BTN_PWR       GPIO_NUM_18
#define PWR_LONG_PRESS_US 1000000
#define IMU_PERIOD_US       16000        /* ~60 Hz is plenty for a menu */

/*
 * The detent. Eighteen degrees to step and eight to re-arm asked for a deliberate lean out and
 * back for every single game, which is a lot of precision to want fourteen times over. Twelve
 * and six is a tilt rather than a heave.
 *
 * Holding past the threshold auto-repeats, and the repeat winds up the longer it is held: a
 * flick moves one game, and leaning on it walks the whole carousel in about a second. Without
 * the wind-up a fixed repeat is either too fast to land on a game or too slow to cross the
 * menu, and there is no rate that is both.
 */
#define NAV_TILT            0            /* 1 to browse by tilt again */
#define NAV_SHORT_PRESS_US  400000       /* a press shorter than this is a step */
#define NAV_ON_DEG          12.0f        /* cross this to step */
#define NAV_OFF_DEG          6.0f        /* fall back inside this to re-arm */
#define NAV_REPEAT_FIRST_US 1000000      /* a full second on the new game before it moves on */
#define NAV_REPEAT_SLOW_US   420000      /* the first repeats, one at a time and readable */
#define NAV_REPEAT_FAST_US   230000      /* what it winds up to - quick, never a blur */
#define NAV_REPEAT_RAMP           8      /* repeats taken to get there: it eases in, not lurches */

/*
 * The zero. It used to be taken from the first sample that passed "held up" - which is the
 * instant the medal leaves the desk, mid-lift, at whatever angle the hand happened to be. Every
 * tilt for the rest of the session was measured against that. The symptom was a carousel that
 * would not stop scrolling until the medal was put down, because "back to centre" was a pose
 * nobody was holding. So now the zero is only taken once the medal has been held still - roll
 * within STILL_DEG for STILL_US - which is a pose someone is actually in. A short press of PWR
 * throws the zero away and the next still moment sets a new one.
 */
#define STILL_DEG   2.5f
#define STILL_US    500000

static bool    imu_ok, have_neutral;
static float   neutral_roll;
static float   still_ref;                /* roll the stillness window is measured from */
static int64_t still_since;              /* and when that window started */
static int64_t imu_last_us;

static int     armed = 1;                /* may a step fire? */
static int     held_dir;                 /* -1, 0, +1 while past threshold */
static int     repeats;                  /* how many the current hold has fired */
static int64_t held_since, last_repeat;
static nav_t   pending_nav = NAV_NONE;

static bool    boot_armed, pwr_armed;          /* seen released since boot? */
static bool    boot_was_down, hold_consumed, pending_hold;
static bool    boot_seen_up;             /* a press only counts once the button has been up after boot */
static int64_t boot_down_since;
static int     hold_ms;
static bool    pwr_was_down;
static bool    chord, pending_mute, pending_wake;
static int64_t pwr_down_since;

/*
 * The direction of gravity within the panel's plane: rolling the medal left or right rotates
 * it, which is exactly the axis we want for browsing.
 *
 * Returns false when that angle cannot be trusted. Lying flat on a desk, gravity points
 * straight out of the screen, the in-plane component is near zero and its atan2 is noise - and
 * that is where the medal is at power-on, next to a USB cable. A neutral captured there is a
 * centre nobody was holding, and the carousel then either runs away or refuses to move.
 */
static bool read_roll(float *deg)
{
    int16_t ax, ay, az;
    qmi8658_read_accel(&ax, &ay, &az);
    float in_plane = sqrtf((float)ax * ax + (float)ay * ay);
    /* the roll swings round by 180 degrees as the panel passes through flat; past about 75
     * degrees from vertical hold the last good value rather than spin the carousel */
    static float last_deg;
    if (in_plane > 0.27f * fabsf((float)az)) last_deg = atan2f((float)ay, (float)ax) * 57.2958f;
    *deg = last_deg;
    /*
     * Held, not lying on a table. This used to demand the medal be within 40 degrees of
     * vertical, then 73, and both were wrong in the same way: a player who looks down at a
     * medal held nearly flat in the palm has a resting posture past the limit, and tipping
     * it further for "up" froze the reading just short of the threshold, so that one
     * direction never registered. The only posture that must be rejected is a medal lying
     * on a table, where the in-plane component of gravity is nothing but noise. So the test
     * is absolute: an eighth of gravity in the plane (about 7 degrees off flat) is enough
     * for the angles, and a zero read from a failed I2C transfer fails it too.
     */
    return in_plane > 2000.0f;
}

static float wrap_deg(float d)
{
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

/* True once `roll` has stayed within STILL_DEG of itself for STILL_US. */
static bool held_still(float roll, int64_t now)
{
    if (fabsf(wrap_deg(roll - still_ref)) > STILL_DEG) { still_ref = roll; still_since = now; return false; }
    return now - still_since >= STILL_US;
}

static void set_neutral(float roll)
{
    neutral_roll = roll;
    have_neutral = true;
    armed = 1; held_dir = 0; repeats = 0;
    ESP_LOGI(TAG, "neutral roll %.1f deg", neutral_roll);
}

/* Forget the zero; the next still moment sets a new one. */
static void clear_neutral(void)
{
    have_neutral = false;
    still_since = esp_timer_get_time();
}

void input_init(void)
{
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_PWR);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    i2c_config_t i2c = {};
    i2c.mode = I2C_MODE_MASTER;
    i2c.sda_io_num = GPIO_NUM_8;
    i2c.scl_io_num = GPIO_NUM_7;
    i2c.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c.master.clk_speed = 100000;
    i2c_param_config(I2C_NUM_0, &i2c);
    esp_err_t err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        ESP_LOGW(TAG, "I2C init failed: %s", esp_err_to_name(err));

    imu_ok = qmi8658_init();
    if (imu_ok) {
        clear_neutral();                  /* the zero comes from the first still moment held up */
    } else {
        ESP_LOGW(TAG, "no IMU - browse with the BOOT button only");
    }
}

bool input_imu_ok(void) { return imu_ok; }

/*
 * The backlight is most of what the battery goes on, so left alone it is turned down, and
 * later off. A button brings it back, and so does being moved - a medal being worn is showing
 * its attract mode to the room and should stay lit; one on a table is showing it to nobody.
 */
#define DIM_AFTER_US    (2 * 60 * 1000000LL)
#define DARK_AFTER_US  (10 * 60 * 1000000LL)
#define DIM_BRIGHTNESS  25                  /* of 255 */
#define MOVED_COUNTS    2500                /* about nine degrees of tilt, at 16384 to the g */

static void screen_tick(int64_t now, bool pressed)
{
    enum { LIT, DIMMED, DARK };
    static int     screen;
    static int64_t touched_us, looked_us;
    static int16_t seen_x, seen_y, seen_z;

    if (imu_ok && now - looked_us >= 100000) {
        looked_us = now;
        int16_t x, y, z;
        qmi8658_read_accel(&x, &y, &z);
        if (abs(x - seen_x) + abs(y - seen_y) + abs(z - seen_z) >= MOVED_COUNTS) {
            seen_x = x; seen_y = y; seen_z = z;
            pressed = true;
        }
    }
    if (pressed) {
        touched_us = now;
        if (screen != LIT) { display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE); screen = LIT; }
        return;
    }
    int want = now - touched_us >= DARK_AFTER_US ? DARK : now - touched_us >= DIM_AFTER_US ? DIMMED : LIT;
    if (want > screen) {
        display_set_backlight(want == DARK ? 0 : DIM_BRIGHTNESS);
        screen = want;
    }
}

void input_poll(void)
{
    int64_t now = esp_timer_get_time();
    bool boot = gpio_get_level(PIN_BTN_BOOT) == 0;
    bool pwr  = gpio_get_level(PIN_BTN_PWR) == 0;
    screen_tick(now, boot || pwr);

    /* ignore whatever was already held when we booted, until it lets go */
    if (!boot_armed) { if (!boot) boot_armed = true; boot = false; }
    if (!pwr_armed)  { if (!pwr)  pwr_armed  = true; pwr  = false; }

    /* Both at once is the mute gesture. It fires as the second button goes down, and
     * from then until both are up again neither button counts as itself - or letting
     * go of them one after the other would step the carousel as well. */
    if (boot && pwr && !chord) { chord = true; pending_mute = true; }
    if (chord) {
        if (!boot && !pwr) chord = false;
        hold_ms = 0;
        hold_consumed = true;
        boot_was_down = pwr_was_down = false;
        return;
    }

    /* BOOT is a hold, not a press. Track how long it has been down so the menu can
     * draw a progress bar, and fire once when it crosses the threshold. A button that
     * was already down when we started is the tail of whatever brought us here - the
     * exit hold from a game, most likely - and is not a press until it has been released. */
    if (!boot) boot_seen_up = true;
    if (!boot_seen_up) { hold_ms = 0; boot_was_down = boot; return; }
    if (boot && !boot_was_down) { boot_down_since = now; hold_consumed = false; }
    /* a short press, released before it could be a hold, steps forward */
    if (!boot && boot_was_down) pending_wake = true;
    if (!boot && boot_was_down && !hold_consumed && now - boot_down_since < NAV_SHORT_PRESS_US)
        pending_nav = NAV_NEXT;
    if (boot) {
        hold_ms = (int)((now - boot_down_since) / 1000);
        if (!hold_consumed && hold_ms >= INPUT_SELECT_HOLD_MS) {
            pending_hold  = true;
            hold_consumed = true;
        }
    } else {
        hold_ms = 0;
    }
    boot_was_down = boot;

    /* PWR: short press re-levels, long press kills battery rail */
    if (pwr && !pwr_was_down) pwr_down_since = now;
    if (pwr && now - pwr_down_since >= PWR_LONG_PRESS_US) {
        battery_power_off();
    }
    if (!pwr && pwr_was_down) pending_wake = true;
    if (!pwr && pwr_was_down && now - pwr_down_since < NAV_SHORT_PRESS_US) pending_nav = NAV_PREV;
    pwr_was_down = pwr;

    if (!NAV_TILT || !imu_ok || now - imu_last_us < IMU_PERIOD_US) return;
    imu_last_us = now;

    float raw;
    /* nothing is captured or acted on until the medal is actually being held up */
    if (!read_roll(&raw)) { still_since = now; still_ref = raw; return; }
    if (!have_neutral) {
        if (held_still(raw, now)) set_neutral(raw);
        return;
    }

    float roll = wrap_deg(raw - neutral_roll);
    int dir = (roll >= NAV_ON_DEG) ? +1 : (roll <= -NAV_ON_DEG) ? -1 : 0;

    if (dir == 0) {
        if (fabsf(roll) <= NAV_OFF_DEG) armed = 1;   /* back to centre: re-arm */
        held_dir = 0;
        repeats  = 0;
        return;
    }

    if (armed && dir != held_dir) {                  /* fresh crossing */
        pending_nav  = (dir > 0) ? NAV_NEXT : NAV_PREV;
        held_dir     = dir;
        held_since   = now;
        last_repeat  = now;
        repeats      = 0;
        armed        = 0;
    } else if (dir == held_dir) {                    /* held over: auto-repeat, winding up */
        int64_t gap;
        if (now - held_since < NAV_REPEAT_FIRST_US) {
            gap = NAV_REPEAT_FIRST_US;
        } else if (repeats >= NAV_REPEAT_RAMP) {
            gap = NAV_REPEAT_FAST_US;
        } else {
            int64_t span = NAV_REPEAT_SLOW_US - NAV_REPEAT_FAST_US;
            gap = NAV_REPEAT_SLOW_US - span * repeats / NAV_REPEAT_RAMP;
        }
        if (now - last_repeat >= gap) {
            pending_nav = (dir > 0) ? NAV_NEXT : NAV_PREV;
            last_repeat = now;
            if (repeats < NAV_REPEAT_RAMP) repeats++;
        }
    }
}

nav_t input_take_nav(void)
{
    nav_t n = pending_nav;
    pending_nav = NAV_NONE;
    return n;
}

bool input_button_down(void)
{
    return gpio_get_level(PIN_BTN_BOOT) == 0;
}

int input_hold_ms(void) { return hold_ms; }

bool input_take_hold(void)
{
    bool h = pending_hold;
    pending_hold = false;
    return h;
}

bool input_take_mute(void)
{
    bool m = pending_mute;
    pending_mute = false;
    return m;
}

bool input_take_wake(void)
{
    bool w = pending_wake;
    pending_wake = false;
    return w;
}
