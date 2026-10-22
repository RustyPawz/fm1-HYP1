/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1's regional events: each biome its own disaster, the same few mechanics underneath, and three for every
 * city (a festival, a blackout, a meteor shower). Included by life.c, after the incidents.
 *
 *   forest    WILDFIRE    the fire runs through the woods; buildings in its way catch, the brigade comes; what
 *                         burns stays cleared
 *   desert    DUST DEVIL  a whirl of sand wanders through, taking a roof here and there
 *   alpine    AVALANCHE   the snow lets go off a mountain and slides down into the valley
 *   isles     ERUPTION    the volcano blows: lava runs downhill and ash rains
 *   plains    TORNADO     the civil-defense siren, then a twister across town (more in a storm)
 *   beach     TSUNAMI     the siren, a wave line comes in from the sea, the shore floods, then it drains
 *   river     FLOOD       the river rises over its banks (more in the rain), then goes down
 *
 * The flows (a fire, lava, snow, water) are worked out at the start as a map of when each tile is reached
 * (rg_t, in 50 ms steps): the screen draws from it, buildings meet their fate as it arrives. */

static const char *const REGION_NAME[NBIOME] = {"WILDFIRE", "DUST DEVIL", "AVALANCHE", "ERUPTION", "TORNADO", "TSUNAMI", "FLOOD"};
static const char *const REGION_KNOB[NBIOME] = {"Wildfires", "Dust devils", "Avalanches", "Eruptions", "Tornadoes", "Tsunamis", "Floods"};
static const uint32_t RG_DUR[NBIOME] = {40000u, 18000u, 26000u, 46000u, 18000u, 26000u, 36000u};
#define RG_WARN 5000u                            /* the tsunami's and the tornado's warning */
#define RG_NHIT 48
static uint16_t rg_t[MN] EN_POOL;                /* when the flow reaches each tile (50 ms steps from rg.t0 + 1); 0 never */
static struct {
    uint8_t on, kind, warned, nhit, fallen;
    uint32_t t0, dur, last;
    float x, y, vx, vy, wob;                     /* where (a vortex: its centre now) */
    int x0, y0, x1, y1;                          /* the flow's box */
    uint16_t hit[RG_NHIT], hit_t[RG_NHIT];       /* the buildings in its way, and when it reaches them */
    uint32_t hit_born[RG_NHIT];
    uint8_t hit_done[RG_NHIT];
} rg;
static struct { uint8_t on, fw_w; float x, y; uint32_t t0, next; uint32_t fw_t[4]; float fw_x[4], fw_y[4]; } fest;
static struct { uint8_t on; float x, y, r; uint32_t t0; } blackout;
#define NMETEOR 6
static struct { uint8_t on; uint32_t t0, next, chime; struct { float x, y, vx, vy; uint32_t t0; } m[NMETEOR]; } meteors;

static uint32_t rg_now(void) { return rg.on ? (plat_ms() - rg.t0) / 50u + 1u : 0u; }
static int rg_vortex(void) { return rg.kind == B_DESERT || rg.kind == B_PLAINS; }
static uint32_t rg_flow_start(void) { return rg.kind == B_BEACH ? RG_WARN / 50u : 0u; }

/* the flow: from the sources outward, each step's cost by the kind and the ground; sweeps until it settles */
static int rg_cost(int i, int from)
{
    int x = i % MW, y = i / MW, t = TERR(i), up = (int)c_hgt[i] - (int)c_hgt[from];
    float j = city_hashf(rg.t0, x, y);
    if (t == T_WATER)
        return -1;
    switch (rg.kind) {
    case B_FOREST:                               /* woods burn fast, grass slow, roads stop it mostly */
        if (c_road[i])
            return j < 0.85f ? -1 : 40;
        if (t == T_ROCK)
            return -1;
        return (t == T_FOREST ? 5 : (c_terr[i] & F_TREE) ? 9 : c_bt[i] ? 14 : 34) + (int)(j * 10.0f);
    case B_ALPINE:                               /* snow: fast downhill, not up */
        return 6 + (up > 0 ? up * 2 : 0) + (int)(j * 10.0f);
    case B_VOLCANO:                              /* lava: downhill, slowly, in fingers */
        if (t == T_ROCK && c_hgt[i] > 200)
            return 2;
        return 16 + (up > 0 ? up * 3 : 0) + (int)(j * j * 40.0f);
    case B_BEACH:                                /* the wave: fast, then the land rises against it */
        if (t == T_ROCK)
            return -1;
        return 5 + (int)c_hgt[i] / 30 + (int)(j * 5.0f);
    default:                                     /* the flood: slow, the low ground first */
        if (t == T_ROCK)
            return -1;
        return 18 + (int)c_hgt[i] / 12 + (int)(j * 10.0f);
    }
}
static void rg_flow(uint32_t tmax)
{
    int it, x, y, changed = 1;
    static const int8_t D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (it = 0; it < 40 && changed; it++) {
        changed = 0;
        for (y = (it & 1) ? rg.y1 : rg.y0; (it & 1) ? y >= rg.y0 : y <= rg.y1; y += (it & 1) ? -1 : 1)
            for (x = (it & 1) ? rg.x1 : rg.x0; (it & 1) ? x >= rg.x0 : x <= rg.x1; x += (it & 1) ? -1 : 1) {
                int i = y * MW + x, d;
                for (d = 0; d < 4; d++) {
                    int nx = x + D[d][0], ny = y + D[d][1], n, c;
                    uint32_t t;
                    if (nx < rg.x0 || ny < rg.y0 || nx > rg.x1 || ny > rg.y1)
                        continue;
                    n = ny * MW + nx;
                    if (!rg_t[n] || (c = rg_cost(i, n)) < 0)
                        continue;
                    t = (uint32_t)rg_t[n] + (uint32_t)c;
                    if (t <= tmax && (!rg_t[i] || t < rg_t[i])) {
                        rg_t[i] = (uint16_t)t;
                        changed = 1;
                    }
                }
            }
    }
}
/* the buildings in the flow's way */
static void rg_targets(void)
{
    int x, y;
    rg.nhit = 0;
    for (y = rg.y0; y <= rg.y1; y++)
        for (x = rg.x0; x <= rg.x1 && rg.nhit < RG_NHIT; x++) {
            int i = y * MW + x, si = c_ref[i];
            if (!si || !rg_t[i] || c_st[si].x != x || c_st[si].y != y || c_st[si].type == BT_CIV)
                continue;
            rg.hit[rg.nhit] = (uint16_t)si;
            rg.hit_t[rg.nhit] = rg_t[i];
            rg.hit_born[rg.nhit] = c_st[si].born;
            rg.hit_done[rg.nhit] = 0;
            rg.nhit++;
        }
}
static void rg_box(int cx, int cy, int r)
{
    int k;
    rg.x0 = cx - r < 0 ? 0 : cx - r;
    rg.y0 = cy - r < 0 ? 0 : cy - r;
    rg.x1 = cx + r > MW - 1 ? MW - 1 : cx + r;
    rg.y1 = cy + r > MH - 1 ? MH - 1 : cy + r;
    for (k = 0; k < MN; k++)
        rg_t[k] = 0;
}
/* a tile of a kind near (cx, cy), the nearest first with a little chance; -1 none */
static int rg_near(int cx, int cy, int r, int (*ok)(int))
{
    int best = -1, x, y;
    float bd = 1e9f;
    for (y = cy - r; y <= cy + r; y++)
        for (x = cx - r; x <= cx + r; x++) {
            float d;
            if (!in_map(x, y) || !ok(y * MW + x))
                continue;
            d = lhyp((float)(x - cx), (float)(y - cy)) + lrnd() * 3.0f;
            if (d < bd) {
                bd = d;
                best = y * MW + x;
            }
        }
    return best;
}
static int ok_wood(int i) { return TERR(i) == T_FOREST || ((c_terr[i] & F_TREE) && !c_bt[i]); }
static int ok_slope(int i)                       /* a mountain's foot: rock beside open land */
{
    int x = i % MW, y = i / MW;
    if (TERR(i) != T_ROCK)
        return 0;
    return (in_map(x + 1, y) && !BLOCKED(i + 1)) || (in_map(x - 1, y) && !BLOCKED(i - 1)) || (in_map(x, y + 1) && !BLOCKED(i + MW)) ||
           (in_map(x, y - 1) && !BLOCKED(i - MW));
}
static int ok_river(int i)
{
    int x = i % MW, y = i / MW;
    if (TERR(i) != T_WATER || (c_terr[i] & F_OCEAN))
        return 0;
    return (in_map(x + 1, y) && !BLOCKED(i + 1)) || (in_map(x - 1, y) && !BLOCKED(i - 1)) || (in_map(x, y + 1) && !BLOCKED(i + MW)) ||
           (in_map(x, y - 1) && !BLOCKED(i - MW));
}
static int ok_shore(int i)
{
    int x = i % MW, y = i / MW;
    if (TERR(i) != T_WATER || !(c_terr[i] & F_OCEAN))
        return 0;
    return (in_map(x + 1, y) && !BLOCKED(i + 1)) || (in_map(x - 1, y) && !BLOCKED(i - 1)) || (in_map(x, y + 1) && !BLOCKED(i + MW)) ||
           (in_map(x, y - 1) && !BLOCKED(i - MW));
}

/* the biome's disaster, now (near the camera, or the city) */
static int region_start(void)
{
    int cx = (int)cam.x, cy = (int)cam.y, src;
    if (rg.on)
        return 0;
    memset(&rg, 0, sizeof rg);
    rg.kind = C.biome;
    rg.t0 = plat_ms();
    rg.dur = RG_DUR[rg.kind];
    if (rg_vortex()) {                           /* in from the view's edge, toward the middle of town */
        float a = lrnd() * 6.2832f, R = 16.0f;
        float tx = (float)(C.minx + C.maxx) * 0.5f, ty = (float)(C.miny + C.maxy) * 0.5f;
        rg.x = tx + fm_cosf(a) * R;
        rg.y = ty + fm_sinf(a) * R;
        rg.vx = (tx - rg.x) / R * (rg.kind == B_PLAINS ? 2.0f : 1.6f);
        rg.vy = (ty - rg.y) / R * (rg.kind == B_PLAINS ? 2.0f : 1.6f);
        rg.wob = lrnd() * 6.0f;
    } else {
        switch (rg.kind) {
        case B_FOREST: src = rg_near(cx, cy, 18, ok_wood); break;
        case B_ALPINE: src = rg_near(cx, cy, 22, ok_slope); break;
        case B_VOLCANO: src = C.vx ? C.vy * MW + C.vx : -1; break;
        case B_BEACH: src = rg_near(cx, cy, 24, ok_shore); break;
        default: src = rg_near(cx, cy, 24, ok_river); break;
        }
        if (src < 0)
            return 0;
        rg.x = (float)(src % MW) + 0.5f;
        rg.y = (float)(src / MW) + 0.5f;
        rg_box(src % MW, src / MW, rg.kind == B_BEACH || rg.kind == B_RIVER ? 18 : 15);
        if (rg.kind == B_BEACH || rg.kind == B_RIVER) {   /* the whole shore (or the banks) in the box at once */
            int x, y;
            for (y = rg.y0; y <= rg.y1; y++)
                for (x = rg.x0; x <= rg.x1; x++)
                    if (rg.kind == B_BEACH ? ok_shore(y * MW + x) : ok_river(y * MW + x))
                        rg_t[y * MW + x] = (uint16_t)(1u + rg_flow_start());
            rg_flow(rg_flow_start() + (rg.kind == B_BEACH ? 75u : 190u));
        } else {
            rg_t[src] = 1;
            rg_flow(rg.kind == B_FOREST ? 520u : rg.kind == B_VOLCANO ? 300u : 70u);
        }
        rg_targets();
    }
    rg.on = 1;
    news(N_REGION, rg.kind, 0);
    if (rg.kind == B_VOLCANO || rg.kind == B_ALPINE)
        en_event(E_BOOM, 0, 0);
    if (rg.kind == B_VOLCANO)
        en_event(E_QUAKE, 30, 0);
    return 1;
}

static void rg_hit(int si, uint32_t born)
{
    float p;
    if (!st_alive(si, born) || !proj.par[P_DISASTER])
        return;
    switch (rg.kind) {
    case B_FOREST:
        if (lrnd() < 0.35f) {
            ignite(si);
            return;
        }
        p = 0.4f;
        break;
    case B_VOLCANO: p = 0.9f; break;
    case B_ALPINE: p = 0.65f; break;
    case B_BEACH: p = 0.45f; break;
    case B_RIVER: p = 0.22f; break;
    default: p = rg.kind == B_PLAINS ? 0.7f : 0.3f; break;
    }
    if (lrnd() < p) {
        city_demolish(si, rg.kind == B_VOLCANO || rg.kind == B_FOREST ? 1 : 2);
        rg.fallen++;
    }
}

static void region_update(float dt)
{
    uint32_t now = plat_ms(), e;
    int k;
    if (!rg.on)
        return;
    e = (now - rg.t0) / 50u + 1u;
    if (rg_vortex()) {
        if (rg.kind == B_PLAINS && now - rg.t0 < RG_WARN)   /* the siren first */
            return;
        rg.wob += dt * 1.3f;
        rg.x += (rg.vx + fm_sinf(rg.wob) * 0.9f) * dt;
        rg.y += (rg.vy + fm_cosf(rg.wob * 0.7f) * 0.9f) * dt;
        if (now - rg.last > 150u) {              /* what's under it */
            int x, y, r = rg.kind == B_PLAINS ? 1 : 0;
            rg.last = now;
            for (y = (int)rg.y - r; y <= (int)rg.y + r; y++)
                for (x = (int)rg.x - r; x <= (int)rg.x + r; x++) {
                    int si;
                    if (!in_map(x, y) || !(si = c_ref[y * MW + x]) || c_st[si].type == BT_CIV)
                        continue;
                    if (lrnd() < (rg.kind == B_PLAINS ? 0.25f : 0.08f))
                        rg_hit(si, c_st[si].born);
                }
        }
    } else {
        for (k = 0; k < rg.nhit; k++)
            if (!rg.hit_done[k] && e >= rg.hit_t[k]) {
                rg.hit_done[k] = 1;
                rg_hit(rg.hit[k], rg.hit_born[k]);
            }
        if (rg.kind == B_FOREST && now - rg.last > 250u) {   /* what burnt stays cleared */
            int x, y;
            rg.last = now;
            for (y = rg.y0; y <= rg.y1; y++)
                for (x = rg.x0; x <= rg.x1; x++) {
                    int i = y * MW + x;
                    if (rg_t[i] && e >= (uint32_t)rg_t[i] + 60u && !c_bt[i] && !c_road[i])
                        clear_ground(i);
                }
        }
    }
    if (now - rg.t0 > rg.dur) {
        rg.on = 0;
        news(N_REGION_END, rg.kind, rg.fallen);
    }
}

/* a festival, a blackout, meteors */
static int festival_start(void)
{
    int k, n = 0, pick = -1;
    if (fest.on || !C.nciv)
        return 0;
    for (k = 0; k < C.nciv; k++) {               /* a plaza, a park, the town hall, the stadium */
        int kind = c_st[c_civ[k]].kind;
        if (kind == CV_PLAZA || kind == CV_PARK || kind == CV_TOWNHALL || kind == CV_STADIUM || kind == CV_FIELDS)
            if (lirnd(++n) == 0)
                pick = c_civ[k];
    }
    if (pick < 0)
        pick = c_civ[lirnd(C.nciv)];
    fest.on = 1;
    fest.t0 = plat_ms();
    fest.next = fest.t0 + 1500u;
    fest.x = (float)c_st[pick].x + (float)c_st[pick].size * 0.5f;
    fest.y = (float)c_st[pick].y + (float)c_st[pick].size * 0.5f;
    news(N_FEST, 0, 0);
    en_event(E_FANFARE, 0, 0);
    return 1;
}
static int blackout_start(void)
{
    if (blackout.on)
        return 0;
    blackout.on = 1;
    blackout.t0 = plat_ms();
    blackout.x = cam.x + (lrnd() - 0.5f) * 8.0f;
    blackout.y = cam.y + (lrnd() - 0.5f) * 6.0f;
    blackout.r = 7.0f + lrnd() * 5.0f;
    news(N_BLACKOUT, 0, 0);
    en_event(E_CRUMBLE, 0, 0);
    return 1;
}
static int blackout_at(float x, float y)        /* the lights out here (and flickering back at the end) */
{
    uint32_t el;
    if (!blackout.on || lhyp(x - blackout.x, y - blackout.y) > blackout.r)
        return 0;
    el = plat_ms() - blackout.t0;
    if (el > 24000u)
        return ((el / 90u) * 7u) % 5u < 2u;
    return 1;
}
static int meteors_start(void)
{
    if (meteors.on)
        return 0;
    memset(&meteors, 0, sizeof meteors);
    meteors.on = 1;
    meteors.t0 = meteors.next = plat_ms();
    news(N_METEOR, 0, 0);
    return 1;
}
static void extras_update(void)
{
    uint32_t now = plat_ms();
    int k;
    if (fest.on) {
        if ((int32_t)(now - fest.next) >= 0) {   /* the rockets: more at night */
            fest.next = now + (uint32_t)((day_lit() < 0.4f ? 900.0f : 2600.0f) + lrnd() * 1500.0f);
            fest.fw_t[fest.fw_w & 3] = now;   /* a rocket (the screen draws it) */
            fest.fw_x[fest.fw_w & 3] = (lrnd() - 0.5f) * 5.0f;
            fest.fw_y[fest.fw_w & 3] = lrnd();
            fest.fw_w++;
            if (music_on())
                en_event(E_FIREWORK, 0, 0);
        }
        if (now - fest.t0 > 28000u)
            fest.on = 0;
    }
    if (blackout.on && now - blackout.t0 > 27000u) {
        blackout.on = 0;
        news(N_LIGHTS, 0, 0);
    }
    if (meteors.on) {
        rect_t r = view_rect(0.0f);
        if ((int32_t)(now - meteors.next) >= 0 && now - meteors.t0 < 20000u) {
            meteors.next = now + 400u + (uint32_t)lirnd(1100);
            for (k = 0; k < NMETEOR && meteors.m[k].t0 && now - meteors.m[k].t0 < 900u; k++)
                ;
            if (k < NMETEOR) {
                float a = 0.5f + lrnd() * 0.5f;
                meteors.m[k].x = r.x0 + lrnd() * (r.x1 - r.x0);
                meteors.m[k].y = r.y0 + lrnd() * (r.y1 - r.y0) * 0.5f;
                meteors.m[k].vx = -fm_cosf(a) * 14.0f;
                meteors.m[k].vy = fm_sinf(a) * 14.0f;
                meteors.m[k].t0 = now;
                if (music_on() && now - meteors.chime > 500u) {
                    meteors.chime = now;
                    en_event(E_METEOR, 0, 0);
                }
            }
        }
        if (now - meteors.t0 > 21500u)
            meteors.on = 0;
    }
}
/* the biggest city's own landmark puts on a show: the desert's rocket launches, the plains' plane takes off, the
 * isles' cruise ship sails with its horn; the others light up with fireworks (a festival round them) */
static struct { uint8_t on, kind, done; uint16_t si; uint32_t t0, born, next; float x, y; } show;
#define SHOW_MS 16000u
static int show_site(void)
{
    int k;
    for (k = 0; k < C.nciv; k++)
        if (c_st[c_civ[k]].kind == CV_UNLOCK3 && !(c_st[c_civ[k]].flags & SF_DEAD))
            return c_civ[k];
    return 0;
}
static int show_start(void)
{
    int si = show_site();
    const st_t *s;
    if (show.on || !si)
        return 0;
    s = &c_st[si];
    show.on = 1;
    show.kind = C.biome;
    show.si = (uint16_t)si;
    show.born = s->born;
    show.t0 = plat_ms();
    show.done = 0;
    show.x = (float)s->x + (float)s->size * 0.5f;
    show.y = (float)s->y + (float)s->size * 0.5f;
    switch (show.kind) {
    case B_DESERT: news(N_UNLOCK, 100, 0); break;     /* (a = 100: the countdown) */
    case B_PLAINS: news(N_UNLOCK, 101, 0); en_event(E_JET, 0, 0); break;
    case B_VOLCANO: news(N_UNLOCK, 102, 0); en_event(E_HORN, 0, 0); break;
    default:
        if (!fest.on) {
            festival_start();
            fest.x = show.x;
            fest.y = show.y;
        }
        break;
    }
    return 1;
}
static void show_update(void)
{
    uint32_t now = plat_ms(), el;
    if (!show.on) {                              /* now and then on its own, every few minutes */
        if (show_site() && (int32_t)(now - show.next) >= 0) {
            if (show.next)
                show_start();
            show.next = now + 150000u + (uint32_t)lirnd(150000);
        }
        return;
    }
    el = now - show.t0;
    if (show.kind == B_DESERT && el >= 5000u && !(show.done & 1u)) {   /* lift-off (once) */
        show.done |= 1u;
        en_event(E_LAUNCH, 0, 0);
        news(N_UNLOCK, 103, 0);
    }
    if (show.kind == B_VOLCANO && el >= 1700u && !(show.done & 2u)) {
        show.done |= 2u;
        en_event(E_HORN, 0, 0);
    }
    if (el > SHOW_MS || !st_alive(show.si, show.born))
        show.on = 0;
}
/* where the show's vehicle is: the rocket's height (tiles), the plane along the runway, the ship sailing (0..1) */
static float show_t(void) { return show.on ? (float)(plat_ms() - show.t0) / 1000.0f : -1.0f; }

static void extras_reset(void)
{
    memset(&rg, 0, sizeof rg);
    memset(&fest, 0, sizeof fest);
    memset(&blackout, 0, sizeof blackout);
    memset(&meteors, 0, sizeof meteors);
    memset(&show, 0, sizeof show);
}
