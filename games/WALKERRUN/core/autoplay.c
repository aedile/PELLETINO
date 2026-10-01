/*
 * autoplay.c - see autoplay.h.
 */
#include "autoplay.h"
#include <string.h>
#include <stdlib.h>

/* beam space */
#define PLAY_X0   25
#define PLAY_X1  225
#define PLAY_Y0   50
#define PLAY_Y1  258
#define CX       125
#define CY       155
#define LEAD     1               /* frames to lead a moving target; the velocity estimate is noisy, so keep it small */

/* what things are made of */
#define COL_GREEN 2
#define COL_CYAN  3
#define COL_RED   4
#define TIE_SEGS  94             /* every TIE fighter, exactly */
#define MIN_TARGET_SEGS 8        /* smaller red or green than this is debris or a star */
#define CROSSHAIR_SEGS 16

#define MAX_CLUSTERS 48

/* the trigger: how often it may be pulled. These three are the whole of how "trigger-happy"
 * the autopilot sounds. A shot costs one from a small budget that refills slowly, so the
 * long-run average is the refill rate and a burst is the budget's size. */
#define SHOT_REFILL_US   1000000u    /* one shot a second, on average */
#define SHOT_BURST       3           /* up to this many in quick succession */
#define SHOT_GAP_US      250000u     /* never faster than this, budget or no */
#define PORT_GAP_US      125000u     /* at the exhaust port: eight a second, budget ignored */

typedef struct { int c, n, x0, y0, x1, y1; } cluster_t;

static ap_config_t cfg;
static ap_state_t state;
static uint64_t idle_since, game_since, lost_since, start_since;
static int have_frame;

/* what the last frame held */
static int cross_x, cross_y, have_cross;
static int tgt_x, tgt_y, have_tgt, ntargets;
static int port_ahead;               /* the "EXHAUST PORT AHEAD" banner is up */
static int have_vp, vp_x, vp_y;      /* the trench's vanishing point: where the exhaust port appears */
static int last_tx, last_ty, have_last_tgt, tvx, tvy;   /* the target a frame ago, for leading */
static int in_trench;                /* the trench walls are on screen (held for a while after) */
static int trench_seen_frames;       /* frames since the walls were last positively seen */
static int port_x, port_y, have_port;    /* the exhaust port: a tiny red mark at the end of the trench */
static uint64_t last_fire_us;        /* when the trigger was last pulled: presses are timed, not counted */
static int trench_frames;            /* how long the walls have been up, continuously, in frames */
static int new_frame;                /* ap_frame has run since ap_update last looked */

void ap_init(const ap_config_t *c)
{
    cfg = *c;
    if (!cfg.idle_us)     cfg.idle_us = 100u * 1000000u;
    if (!cfg.lost_us)     cfg.lost_us = 10u * 1000000u;
    if (!cfg.max_game_us) cfg.max_game_us = 900u * 1000000u;
    state = AP_IDLE;
    idle_since = game_since = lost_since = start_since = 0;
    have_cross = have_tgt = ntargets = 0;
    have_port = 0; trench_seen_frames = 1000; trench_frames = 0; last_fire_us = 0;
}

ap_state_t ap_state(void) { return state; }
int ap_targets(void) { return ntargets; }
void ap_crosshair(int *x, int *y) { *x = cross_x; *y = cross_y; }
int  ap_have_cross(void) { return have_cross; }
int  ap_port_ahead(void) { return port_ahead; }
int  ap_in_trench(void) { return in_trench; }
void ap_target(int *x, int *y, int *have) { *x = tgt_x; *y = tgt_y; *have = have_tgt; }
void ap_port(int *x, int *y, int *have) { *x = port_x; *y = port_y; *have = have_port; }

/*
 * Group the drawn segments of one colour into clusters by endpoint proximity. This is the
 * whole of the "vision": a TIE fighter is a green cluster of 94, a fireball a red cluster in
 * the play area, the crosshair a cyan cluster of 16. Quadratic in the number of segments of
 * a colour, which is a few hundred at most, once a frame - cheap enough.
 */
static int gather(const avg_t *avg, int colour, cluster_t *out, int max)
{
    int n = 0;
    int px = 0, py = 0;
    for (int i = 0; i < avg->npoints; i++) {
        const avg_point_t *p = &avg->points[i];
        int x = (int)(p->x >> 16), y = (int)(p->y >> 16);
        if (i && p->intensity && p->color == colour) {
            int sx0 = px < x ? px : x, sx1 = px < x ? x : px;
            int sy0 = py < y ? py : y, sy1 = py < y ? y : py;
            /* attach to a cluster this segment touches (within 6), else start one */
            int k;
            for (k = 0; k < n; k++) {
                cluster_t *c = &out[k];
                if (sx1 >= c->x0 - 6 && sx0 <= c->x1 + 6 && sy1 >= c->y0 - 6 && sy0 <= c->y1 + 6) break;
            }
            if (k == n) {
                if (n == max) { px = x; py = y; continue; }
                out[n++] = (cluster_t){ colour, 0, sx0, sy0, sx1, sy1 };
            }
            cluster_t *c = &out[k];
            c->n++;
            if (sx0 < c->x0) c->x0 = sx0;
            if (sy0 < c->y0) c->y0 = sy0;
            if (sx1 > c->x1) c->x1 = sx1;
            if (sy1 > c->y1) c->y1 = sy1;
        }
        px = x; py = y;
    }
    /* a segment can bridge two clusters that were started apart; one merge pass is enough here */
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++) {
            if (out[b].n == 0) continue;
            if (out[b].x1 >= out[a].x0 - 6 && out[b].x0 <= out[a].x1 + 6 &&
                out[b].y1 >= out[a].y0 - 6 && out[b].y0 <= out[a].y1 + 6) {
                out[a].n += out[b].n;
                if (out[b].x0 < out[a].x0) out[a].x0 = out[b].x0;
                if (out[b].y0 < out[a].y0) out[a].y0 = out[b].y0;
                if (out[b].x1 > out[a].x1) out[a].x1 = out[b].x1;
                if (out[b].y1 > out[a].y1) out[a].y1 = out[b].y1;
                out[b].n = 0;
            }
        }
    return n;
}

static int in_play(const cluster_t *c)
{
    int cx = (c->x0 + c->x1) / 2, cy = (c->y0 + c->y1) / 2;
    return cx >= PLAY_X0 && cx <= PLAY_X1 && cy >= PLAY_Y0 && cy <= PLAY_Y1;
}

/*
 * The banner line under the score ("USE THE FORCE", "EXHAUST PORT AHEAD", "EXHAUST PORT
 * MISSED", "SHIELD GONE") changes colour every frame, so it is read by shape: the words are
 * runs of drawn columns in the band y 30..70, split at gaps of a dozen pixels or more, and
 * "EXHAUST PORT AHEAD" is three words 56, 39 and 48 pixels wide, left to right. "MISSED" is
 * 60 wide, which keeps the two apart.
 */
static int banner_port_ahead(const avg_t *avg)
{
    uint8_t col[256]; memset(col, 0, sizeof col);
    for (int i = 0; i < avg->npoints; i++) {
        const avg_point_t *q = &avg->points[i];
        int x = (int)(q->x >> 16), y = (int)(q->y >> 16);
        if (q->intensity && y >= 45 && y <= 66 && x >= 0 && x < 256) col[x] = 1;
    }
    int w[8], nw = 0, x0 = -1, last = -100;
    for (int x = 0; x < 256; x++) {
        if (!col[x]) continue;
        if (x - last >= 14) { if (x0 >= 0 && nw < 8) w[nw++] = last - x0; x0 = x; }
        last = x;
    }
    if (x0 >= 0 && nw < 8) w[nw++] = last - x0;
    /* measured on the harness: EXHAUST PORT AHEAD = 80, 43, 56 (the first run includes a
     * cockpit mark to its left); EXHAUST PORT MISSED = 80, 43, 68; and now and then the last
     * two words run together, 115 for AHEAD against 127 for MISSED */
    if (nw == 3) return w[0] >= 50 && abs(w[1] - 43) <= 8 && abs(w[2] - 56) <= 6;
    if (nw == 2) return w[0] >= 50 && abs(w[1] - 115) <= 6;
    return 0;
}

void ap_frame(const avg_t *avg)
{
    new_frame = 1;
    static cluster_t cl[MAX_CLUSTERS];
    have_frame = 1;

    /*
     * The crosshair: sixteen cyan segments in a sixteen-pixel box. "USE THE FORCE" is cyan
     * too, at the top of the play area, and its words are about the same segment count, so
     * the shape is checked as well as the count; with more than one candidate, the nearest
     * to where it was a frame ago wins.
     */
    int n = gather(avg, COL_CYAN, cl, MAX_CLUSTERS);
    int prev_x = cross_x, prev_y = cross_y, prev_ok = have_cross;
    have_cross = 0;
    int nearest = 0x7fffffff;
    for (int i = 0; i < n; i++) {
        if (cl[i].n < CROSSHAIR_SEGS - 3 || cl[i].n > CROSSHAIR_SEGS + 3) continue;
        int w = cl[i].x1 - cl[i].x0, h = cl[i].y1 - cl[i].y0;
        if (w < 11 || w > 20 || h < 11 || h > 20) continue;      /* the crosshair is a 16x16 box; text is wide and short */
        int cx = (cl[i].x0 + cl[i].x1) / 2, cy = (cl[i].y0 + cl[i].y1) / 2;
        int d = prev_ok ? abs(cx - prev_x) + abs(cy - prev_y) : 0;
        if (!have_cross || d < nearest) { cross_x = cx; cross_y = cy; have_cross = 1; nearest = d; }
    }

    /*
     * The trench. The walls are one green cluster spanning the screen. Seeing them is not
     * reliable every frame - a wall can drop out of the list as it scrolls past, and for
     * seconds at a time when the ship is hard against one side - so the flag is held for
     * three seconds after the last positive sighting rather than flickering.
     */
    n = gather(avg, COL_GREEN, cl, MAX_CLUSTERS);
    int walls = 0;
    for (int i = 0; i < n; i++)
        if (cl[i].n >= 25 && cl[i].x1 - cl[i].x0 >= 200 && cl[i].y1 - cl[i].y0 >= 150) walls = 1;
    if (walls) trench_seen_frames = 0; else if (trench_seen_frames < 1000) trench_seen_frames++;
    in_trench = trench_seen_frames < 100;
    trench_frames = in_trench ? trench_frames + 1 : 0;

    /*
     * The exhaust port. It shows for about a second at the end of each trench run: a tiny
     * red mark, four segments, at the point the walls converge, growing into the base of
     * the end wall as it rushes up. The "EXHAUST PORT AHEAD" banner comes up with it, and
     * that is read by shape (above) since its colour changes every frame. The mark is
     * accepted on its own only once the trench has been up a few seconds: on the way in,
     * over the surface, other small red things pass through the same box.
     */
    have_port = 0; port_ahead = 0;
    if (in_trench) {
        port_ahead = banner_port_ahead(avg);
        n = gather(avg, COL_RED, cl, MAX_CLUSTERS);
        for (int i = 0; i < n; i++) {
            const cluster_t *c = &cl[i];
            if (c->n < 2 || c->n > 16) continue;
            if (c->x1 - c->x0 > 24 || c->y1 - c->y0 > 24) continue;
            int cx = (c->x0 + c->x1) / 2, cy = (c->y0 + c->y1) / 2;
            if (cx < 85 || cx > 165 || cy < 150 || cy > 255) continue;
            if (!port_ahead && trench_frames < 120) continue;
            port_x = cx; port_y = cy; have_port = 1;
            break;
        }
    }

    /*
     * The exhaust port has no mark of its own - it sits at the point the trench walls converge
     * to. So when the walls are up, find that point: every long, sloped green segment lies on a
     * line, and the least-squares intersection of those lines is the vanishing point, steady at
     * about (128,165). That is where the port will be, and where to hold the crosshair and the
     * trigger. Aiming at a fixed guess missed it by thirty pixels; this tracks it.
     */
    have_vp = 0;
    if (in_trench) {
        double Saa = 0, Sab = 0, Sbb = 0, Sac = 0, Sbc = 0; int m = 0;
        int px = 0, py = 0;
        for (int i = 0; i < avg->npoints; i++) {
            const avg_point_t *q = &avg->points[i];
            int x = (int)(q->x >> 16), y = (int)(q->y >> 16);
            if (i && q->intensity && q->color == COL_GREEN) {
                int dx = x - px, dy = y - py;
                if (abs(dx) + abs(dy) > 15 && (abs(dx) > 3 || abs(dy) > 3)) {
                    double a = dy, b = -dx, nn = a * a + b * b;
                    if (nn > 1) {
                        nn = 1.0 / nn;                       /* work in a^2+b^2 = 1/nn units */
                        double c = a * px + b * py;
                        Saa += a * a * nn; Sab += a * b * nn; Sbb += b * b * nn;
                        Sac += a * c * nn; Sbc += b * c * nn; m++;
                    }
                }
            }
            px = x; py = y;
        }
        double det = Saa * Sbb - Sab * Sab;
        if (m >= 6 && (det > 1e-3 || det < -1e-3)) {
            int vx = (int)((Sbb * Sac - Sab * Sbc) / det);
            int vy = (int)((Saa * Sbc - Sab * Sac) / det);
            if (vx >= PLAY_X0 && vx <= PLAY_X1 && vy >= PLAY_Y0 && vy <= PLAY_Y1) { vp_x = vx; vp_y = vy; have_vp = 1; }
        }
    }

    /*
     * Targets. Fireballs first - a red cluster in the play area is on its way to the shields
     * and shooting it is what the game is about - then the nearest TIE fighter. Anything
     * outside the play area is cockpit, and any small red thing is a laser bolt or a spark.
     */
    int best = -1, bestd = 0x7fffffff, bestpri = 0;
    last_tx = tgt_x; last_ty = tgt_y; have_last_tgt = have_tgt;
    have_tgt = 0; ntargets = 0;
    int ax = have_cross ? cross_x : CX, ay = have_cross ? cross_y : CY;
    for (int pass = 0; pass < 2; pass++) {
        int colour = pass == 0 ? COL_RED : COL_GREEN;
        n = gather(avg, colour, cl, MAX_CLUSTERS);
        for (int i = 0; i < n; i++) {
            const cluster_t *c = &cl[i];
            if (c->n < MIN_TARGET_SEGS || !in_play(c)) continue;
            if (c->y1 < 70 && c->x1 - c->x0 > 40) continue;    /* a line of banner text under the score */
            if (colour == COL_RED && in_trench) {
                /* the trench walls are lined with laser turrets: tall thin red things, up to a
                 * hundred segments in a column a few pixels wide. Shooting them is points, and
                 * a pilot who shoots every one of them never stops firing, which is the one
                 * thing this pilot must not do. The fireballs they throw are the danger, and
                 * those are as wide as they are tall. So: tall and thin is left alone. */
                int w = c->x1 - c->x0, h = c->y1 - c->y0;
                if (h > 2 * w + 6) continue;
            }
            if (colour == COL_GREEN) {
                /* a TIE is exactly 94 segments, or 188 when two overlap, and never wider than a
                 * hand's breadth; the trench walls are green and span the screen */
                int ok = (c->n >= TIE_SEGS - 6 && c->n <= TIE_SEGS + 6) || (c->n >= 2 * TIE_SEGS - 10 && c->n <= 2 * TIE_SEGS + 10);
                if (!ok || c->x1 - c->x0 > 70 || c->y1 - c->y0 > 70) continue;
            }
            ntargets++;
            int pri = (colour == COL_RED) ? 2 : 1;
            int cx = (c->x0 + c->x1) / 2, cy = (c->y0 + c->y1) / 2;
            int d = abs(cx - ax) + abs(cy - ay);
            if (pri > bestpri || (pri == bestpri && d < bestd)) { best = i; bestd = d; bestpri = pri; tgt_x = cx; tgt_y = cy; have_tgt = 1; }
        }
        if (have_tgt && pass == 0) break;            /* a fireball wins outright */
    }
    (void)best;
    /* if this frame's target is near last frame's, it is the same one, so its velocity is real */
    if (have_tgt && have_last_tgt && abs(tgt_x - last_tx) + abs(tgt_y - last_ty) < 45) {
        tvx = tgt_x - last_tx; tvy = tgt_y - last_ty;
    } else { tvx = tvy = 0; }
}

void ap_update(sw_input_t *in, uint64_t now_us, int human_active)
{
    if (!idle_since) idle_since = now_us;

    if (human_active) {
        /* a person: hands off, and stay off until their game is done and the attract has
         * been left alone again */
        state = AP_HUMAN;
        idle_since = now_us;
        return;
    }

    switch (state) {
    case AP_HUMAN:
        /* their game ends when the crosshair has been gone a while; then it is an attract
         * nobody is touching, and the idle clock starts over */
        if (!have_cross) { if (!lost_since) lost_since = now_us; }
        else lost_since = 0;
        if (lost_since && now_us - lost_since > cfg.lost_us) { state = AP_IDLE; idle_since = now_us; lost_since = 0; }
        return;

    case AP_IDLE:
        if (now_us - idle_since >= cfg.idle_us) { state = AP_STARTING; start_since = now_us; }
        return;

    case AP_STARTING:
        /* free play: a pull of the trigger starts the game */
        in->fire = (now_us - start_since) < 400000;
        if (now_us - start_since > 1500000) { state = AP_PLAYING; game_since = now_us; lost_since = 0; }
        return;

    case AP_PLAYING: {
        /* the game is over when the crosshair has been gone a while, or we have flown long
         * enough; the select screen at the start does not count, its crosshair is odd */
        uint64_t t_game = now_us - game_since;
        int selecting = t_game < 13000000u;
        int sweeping  = t_game >= 13000000u && t_game < 15000000u;
        if (!have_cross && !selecting) { if (!lost_since) lost_since = now_us; }
        else lost_since = 0;
        if ((lost_since && now_us - lost_since > cfg.lost_us) || now_us - game_since > cfg.max_game_us) {
            state = AP_IDLE; idle_since = now_us; lost_since = 0;
            in->yaw = in->pitch = 0x80; in->fire = 0;
            return;
        }
        /*
         * Where to aim. The first dozen seconds of a game are the "select a Death Star" screen:
         * three green Death Stars and red labels, and a shot while the crosshair is on one of
         * them picks that difficulty. Left alone, the countdown picks Easy, which is the one
         * this pilot can fly. So: centre and no trigger until the countdown has run out.
         * After that: the exhaust port when it is there, else the nearest threat - a fireball,
         * a turret, a TIE - led a frame by its own motion, else (in the trench, between
         * threats) a spot a little below the vanishing point, where the port will show up.
         */
        int aim_x = CX, aim_y = CY, shoot_here = 0, tol = 20;
        if (selecting)                   { aim_x = CX; aim_y = CY; }
        else if (sweeping) {
            /*
             * The game calibrates the yoke itself: it takes the least and greatest readings it
             * has seen as the ends of the yoke's travel, and maps those to the edges of the
             * screen. Until it has seen the ends, the map is whatever range it has seen so
             * far, stretched to fit - which for an autopilot that only ever nudges is a map
             * off by forty pixels and a quarter again too steep. So, the moment the countdown
             * is done and before anything shoots back: a second of yoke to all four corners.
             * From then on the map below holds.
             */
            int corner = (int)((t_game - 13000000u) / 500000u) & 3;
            in->yaw   = (corner == 1 || corner == 2) ? 255 : 0;
            in->pitch = (corner >= 2) ? 255 : 0;
            in->fire = 0;
            return;
        }
        else if (in_trench && have_port)  { aim_x = port_x; aim_y = port_y; shoot_here = 1; tol = 20; }
        else if (in_trench && port_ahead) { aim_x = have_vp ? vp_x : CX; aim_y = (have_vp ? vp_y : CY) + 4; shoot_here = 1; tol = 30; }
        else if (have_tgt)                { aim_x = tgt_x + tvx * LEAD; aim_y = tgt_y + tvy * LEAD; shoot_here = 1; }
        else if (in_trench && have_vp)    { aim_x = vp_x; aim_y = vp_y + 4; }     /* where the port will show */
        else if (in_trench)               { aim_x = CX;   aim_y = CY + 4; }
        /*
         * The yoke is an absolute position, not a rate: the crosshair goes where the yoke is,
         * settling in well under half a second, in every phase. Measured on the harness with
         * the yoke held still: yaw 0 puts the crosshair at x=13, 255 at x=236, 128 at x=125;
         * pitch 255 puts it at y=55, 0 at y=268, 128 at y=133. So steering is the inverse of
         * that map, plus a slow trim from the crosshair actually seen, in case the map is a
         * pixel or two off somewhere.
         */
        int new_frame_seen = new_frame;
        static int trim_x, trim_y;
        if (have_cross && !selecting && new_frame) {
            int ex = aim_x - cross_x, ey = aim_y - cross_y;
            if (abs(ex) < 40) trim_x += (ex > 0) - (ex < 0);
            if (abs(ey) < 40) trim_y += (ey > 0) - (ey < 0);
            if (trim_x > 30) trim_x = 30;
            if (trim_x < -30) trim_x = -30;
            if (trim_y > 30) trim_y = 30;
            if (trim_y < -30) trim_y = -30;
        }
        new_frame = 0;
        int yaw   = (aim_x + trim_x - 13) * 255 / 223;
        int pitch = (268 - (aim_y + trim_y)) * 255 / 213;
        if (yaw < 0) yaw = 0;
        if (yaw > 255) yaw = 255;
        if (pitch < 0) pitch = 0;
        if (pitch > 255) pitch = 255;
        in->yaw = (uint8_t)yaw;
        in->pitch = (uint8_t)pitch;
        /*
         * The trigger. Holding it, or pulsing it every frame, is a barrage that drowns the
         * music and sounds nothing like a pilot. So: a short pull, only with the crosshair on
         * something worth a shot, and out of a small budget that keeps the average down to
         * about one a second (the SHOT_ defines above) - except at the exhaust port, which is
         * small and passes quickly. Timed in microseconds so the medal, which runs
         * this once a display frame, and the harness, which runs it far more often, agree.
         */
        /*
         * The crosshair vanishes from the reader for a frame or a few now and then - most
         * tellingly at the exhaust port, whose mark is drawn in the same cyan right where the
         * crosshair is waiting, so the two merge and neither is recognised. The yoke is an
         * absolute position and the crosshair does not move on its own, so where it was a
         * moment ago is where it still is: remember it for a second.
         */
        static int mem_cx, mem_cy, mem_age = 1000;
        if (have_cross) { mem_cx = cross_x; mem_cy = cross_y; mem_age = 0; }
        else if (new_frame_seen && mem_age < 1000) mem_age++;
        int use_cross = have_cross || mem_age < 40;
        int ex = use_cross ? aim_x - mem_cx : 999;
        int ey = use_cross ? aim_y - mem_cy : 999;
        int on_target = shoot_here && abs(ex) < tol && abs(ey) < tol;
        int port_mode = in_trench && (have_port || port_ahead);
        uint64_t period = port_mode ? PORT_GAP_US : SHOT_GAP_US;
        /* the budget (see SHOT_REFILL_US); the exhaust port is exempt, that second is what the
         * whole run is for */
        static uint64_t budget_us;           /* the clock the budget was last settled at */
        static int shots_in_hand;
        if (!budget_us) { budget_us = now_us; shots_in_hand = SHOT_BURST; }
        static uint64_t refill_acc;
        refill_acc += now_us - budget_us; budget_us = now_us;
        while (refill_acc >= SHOT_REFILL_US) { refill_acc -= SHOT_REFILL_US; if (shots_in_hand < SHOT_BURST) shots_in_hand++; }
        int may_fire = port_mode || shots_in_hand > 0;
        if (on_target && may_fire && now_us - last_fire_us >= period) {
            last_fire_us = now_us;
            if (!port_mode) shots_in_hand--;
        }
        in->fire = (now_us - last_fire_us) < 60000u && last_fire_us != 0;
        return;
    }
    }
}
