# medal-input

**The part of a Fiesta medal's controls that every game shares.**

A San Antonio Fiesta medal is a collectible pin. These particular ones are
Waveshare ESP32-C6-LCD-1.69 boards running arcade emulators — Pac-Man, Donkey
Kong, Missile Command, Arkanoid and the rest. Every one of them does the same
things with the same hardware, and this is that part, factored out so a fix
lands once instead of eleven times.

---

## What it handles

- The **battery rail** (GPIO 15), which the firmware has to hold up or the medal
  switches itself off.
- The **two buttons** — BOOT (GPIO 9) and PWR (GPIO 18) — with their hold times.
- **Coin, then start**: a short press of PWR inserts a coin and presses start
  half a second later, which is the whole of "put a quarter in" on a pin with no
  coin slot.
- **Power off** on a long press of PWR.
- **Sound** by pressing both buttons together. These get worn places, and some
  of those places need to be quiet.
- **The backlight**: it dims after two minutes with no button and no movement,
  and comes back with either.
- **The tilt zero** — see below, because this is the part that is easy to get
  wrong and it took a while to notice.

## What it deliberately does not handle

What the tilt *means*. A trackball wants a rate, a four-way stick wants a
direction with hysteresis, a spinner wants an absolute position, a yoke wants
two axes of it. That is the interesting part of each game's controls and it
stays in each game. This component hands over two trustworthy angles and gets
out of the way.

## The tilt model

A medal is held upright, like a phone, so gravity lies in the plane of the
panel. Two angles come out of that:

| | | |
|---|---|---|
| `lr` | `atan2(ay, ax)` | The direction of gravity **within** the panel's plane. Rotating the medal in your fingers — a twist — sweeps this through 360°. This is the axis a spinner, a steering wheel and a left/right stick all use. |
| `ud` | `atan2(az, hypot(ax, ay))` | How far gravity **leaves** that plane. Tipping the top edge toward you or away from you sweeps this. Up and down. |

Both are reported relative to a captured neutral pose, so "however I am holding
it right now" is centre.

### Why the pose has to be checked

Those angles only mean anything while the medal is actually being held up.
**Lying flat on a desk, gravity points straight out of the screen**, the
in-plane component is near zero, and its `atan2` is noise.

A neutral pose captured there is a centre the player was never holding. On a
threshold control — a four-way stick — that is merely wrong. On an *absolute*
one it is much worse: Arkanoid's paddle sat pinned against one end of its
travel, so tilting right did nothing at all and tilting left swept the entire
playfield the instant it crossed back into range. The zero was being captured on
the first IMU read, which happens at boot, which is while the medal is on a desk
next to a USB cable.

So nothing is captured or reported until gravity is genuinely in the plane
(`hypot(ax, ay) > 1.2 · |az|`). The zero is taken at the first such moment, again
on every coin, and again when start goes in half a second later — by which point
the player has settled into playing posture. Until then the last trusted angles
are held rather than letting the controls swing on noise.

---

## Using it

Copy `components/medal_input/` into your project's `components/`, add
`medal_input` to your `main` component's `REQUIRES`, and:

```c
#include "medal_input.h"
#include "qmi8658.h"

static void on_mute(void) { audio_set_mute(!audio_get_mute()); }
static void on_recentre(void) { /* clear whatever your mapping has latched */ }

void input_init(void)
{
    medal_input_config_t cfg = {};
    cfg.init_i2c     = true;              /* I2C0 on GPIO 8/7 */
    cfg.imu_init     = qmi8658_init;      /* called after I2C is up */
    cfg.read_accel   = qmi8658_read_accel;
    cfg.on_mute      = on_mute;
    cfg.on_recentre  = on_recentre;
    medal_input_init(&cfg);
}

void input_update(my_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    if (st.tilt_fresh) {
        /* st.lr and st.ud are degrees from centre. Your game's mapping goes here. */
    }
    in->coin1  = st.coin;
    in->start1 = st.start;
    in->fire   = st.boot;
}
```

`on_recentre` matters: when the zero moves, anything your mapping has latched —
accumulated trackball counts, a sticky axis, a filtered paddle position — should
go back to centre with it, or the control jumps.

### Configuration

| Field | Default | |
|---|---|---|
| `imu_period_us` | 16000 | How often the IMU is read. A paddle wants nearer 5000. |
| `power_off_hold_us` | 1000000 | PWR hold that cuts the battery rail. |
| `mute_hold_us` | | Retired. The sound is both buttons together, and `on_mute` is called for that. |
| `coin_us` / `gap_us` / `start_us` | 100000 / 400000 / 100000 | The coin-then-start sequence. |

### Gestures of your own

`medal_input_state_t` reports `boot_held_us`, `pwr_held_us`, `boot_released` and
`boot_release_held_us`, so a game can add gestures the others do not have
without forking the component.

---

## Which medals use it

[PELLETINO](https://github.com/aedile/PELLETINO) (Pac-Man / Ms. Pac-Man),
[GIRDER](https://github.com/aedile/GIRDER) (Donkey Kong),
[SILO](https://github.com/aedile/SILO) (Missile Command),
[AEROLITE](https://github.com/aedile/AEROLITE) (Asteroids),
[CHILOPODA](https://github.com/aedile/CHILOPODA) (Centipede),
[RIBBIT](https://github.com/aedile/RIBBIT) (Frogger),
[SMOKESCREEN](https://github.com/aedile/SMOKESCREEN) (Rally-X),
[STRATUM](https://github.com/aedile/STRATUM) (Dig Dug),
[VAUS](https://github.com/aedile/VAUS) (Arkanoid),
[TRENCHRUNNER](https://github.com/aedile/TRENCHRUNNER) (Star Wars),
[QUALIFIER](https://github.com/aedile/QUALIFIER) (Pole Position).

## License

[0BSD](LICENSE). No game ROMs here — this is a button and accelerometer driver.
