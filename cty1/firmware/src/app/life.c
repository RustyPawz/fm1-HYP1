/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1: the city's life, in the main loop (real time, not the growth's weeks): the clock and the seasons, the
 * weather, birds, cars and people (with what they say to each other), and the incidents (lightning and fire,
 * earthquakes, robberies, crashes, the odd UFO) with the trucks and cars that answer them. None of it changes
 * how a seed grows, except what fires and quakes knock down. It feeds the music: the cars on screen are its
 * playheads, the rest its colour (en_city, en_event).
 *
 * Part of the unity build (ui.c includes it after city.c); the camera is ui.c's (cam). */

/* ------------------------------------------------------------ camera --- */
/* where the main loop is, for a watchdog reset to tell (kept through the reset: main_fm1.c shows it at boot) */
static volatile uint32_t ui_where __attribute__((section(".noinit")));
#define WHERE(n) (ui_where = (uint32_t)(n))
static struct {
    float x, y;                         /* the view's middle, in tiles */
    int ts;                             /* pixels a tile */
    int vw, vh;                         /* the view, in pixels */
} cam = {48.0f, 40.0f, 8, 240, 144};
static int cam_ts(void) { return cam.ts > 0 ? cam.ts : 8; }   /* (never 0: everything divides by it) */
typedef struct { float x0, y0, x1, y1; } rect_t;
static rect_t view_rect(float m)
{
    rect_t r;
    float hw = (float)cam.vw / (float)cam_ts() * 0.5f, hh = (float)cam.vh / (float)cam_ts() * 0.5f;
    if (!(cam.x > -200.0f && cam.x < 300.0f && cam.y > -200.0f && cam.y < 300.0f)) {   /* (NaN too): back to the middle */
        cam.x = (float)MW * 0.5f;
        cam.y = (float)MH * 0.5f;
    }
    r.x0 = cam.x - hw - m;
    r.y0 = cam.y - hh - m;
    r.x1 = cam.x + hw + m;
    r.y1 = cam.y + hh + m;
    return r;
}
static int in_rect(const rect_t *r, float x, float y) { return x >= r->x0 && y >= r->y0 && x <= r->x1 && y <= r->y1; }

/* ------------------------------------------------------------ random --- */
static uint32_t lrs = 0x1234567u;
static uint32_t lrnd_u(void)
{
    lrs ^= lrs << 13;
    lrs ^= lrs >> 17;
    lrs ^= lrs << 5;
    return lrs;
}
static float lrnd(void) { return (float)(lrnd_u() >> 8) * (1.0f / 16777216.0f); }
static int lirnd(int n) { return n > 0 ? (int)mulhi32(lrnd_u(), (uint32_t)n) : 0; }
static float lhyp(float x, float y) { return fm_sqrtf(x * x + y * y); }

/* ---------------------------------------------------- time and seasons --- */
static const uint32_t DAY_MS[5] = {120000u, 240000u, 480000u, 960000u, 0u};
static struct {
    float clock;                        /* ms into the day */
    float days;                         /* days gone by (the seasons) */
    uint8_t frozen;                     /* always noon */
    float last_phase;
} tm = {0.29f * 240000.0f, 0.0f, 0, 0.29f};
static uint32_t day_len(void) { return DAY_MS[(unsigned)proj.par[P_DAYLEN] < 5u ? proj.par[P_DAYLEN] : 1]; }
static float phase(void)
{
    uint32_t L = day_len();
    return L ? tm.clock / (float)L : 0.5f;
}
static void set_phase(float p)
{
    uint32_t L = day_len() ? day_len() : 240000u;
    tm.clock = (p - fm_floorf(p)) * (float)L;
}
static float day_lit(void)
{
    float b = 0.5f - 0.5f * fm_cosf(6.28318548f * phase());
    return fm_clampf((b - 0.25f) / 0.5f, 0.0f, 1.0f);
}
static float dusk(void)
{
    float p = phase(), a = (p - 0.27f) / 0.035f, b = (p - 0.76f) / 0.035f;
    if (!day_len())
        return 0.0f;
    return fm_minf(1.0f, fm_expf(-a * a) + fm_expf(-b * b));
}
static void clock_text(char *b)
{
    int m = (int)(phase() * 1440.0f) % 1440;
    b[0] = (char)('0' + m / 600);
    b[1] = (char)('0' + m / 60 % 10);
    b[2] = ':';
    b[3] = (char)('0' + m % 60 / 10);
    b[4] = (char)('0' + m % 10);
    b[5] = 0;
}

/* the seasons' colours: grass (five heights), woods, leaves, yards, parks, the river */
enum { SC_G0, SC_G1, SC_G2, SC_G3, SC_G4, SC_FOREST, SC_LEAF, SC_LEAF2, SC_LEAFHI, SC_LEAFMID, SC_YARD, SC_PARK, SC_RIVER, SC_N };
static const uint16_t SEASON_PAL[4][SC_N] = {
    {RGB(111, 168, 79), RGB(121, 178, 87), RGB(130, 185, 94), RGB(140, 189, 102), RGB(152, 187, 110), RGB(77, 138, 62), RGB(74, 143, 60),
     RGB(53, 122, 44), RGB(124, 194, 94), RGB(90, 158, 72), RGB(130, 184, 96), RGB(111, 184, 90), RGB(58, 120, 166)},
    {RGB(106, 152, 80), RGB(116, 161, 87), RGB(125, 168, 93), RGB(135, 173, 99), RGB(148, 174, 106), RGB(74, 122, 60), RGB(63, 122, 54),
     RGB(47, 97, 41), RGB(92, 154, 73), RGB(79, 138, 64), RGB(127, 174, 93), RGB(106, 174, 86), RGB(56, 113, 160)},
    {RGB(138, 154, 82), RGB(147, 160, 87), RGB(156, 165, 92), RGB(168, 168, 98), RGB(176, 168, 104), RGB(122, 106, 52), RGB(184, 100, 42),
     RGB(122, 68, 34), RGB(224, 160, 64), RGB(201, 138, 58), RGB(148, 160, 90), RGB(143, 164, 86), RGB(58, 110, 150)},
    {RGB(223, 230, 234), RGB(230, 236, 239), RGB(237, 241, 243), RGB(242, 245, 246), RGB(247, 249, 250), RGB(201, 211, 214), RGB(47, 90, 58),
     RGB(36, 69, 44), RGB(232, 238, 240), RGB(61, 106, 72), RGB(232, 237, 240), RGB(226, 233, 236), RGB(143, 182, 207)},
};
static uint16_t SEAS[SC_N];
static struct { uint8_t idx, vis; float snow; } season = {1, 1, 0.0f};   /* vis: the season the land shows */
static const char *const SEASON_NAME[4] = {"spring", "summer", "autumn", "winter"};

static uint16_t lerp565(uint16_t a, uint16_t b, float f)
{
    int t = (int)(f * 16.0f + 0.5f), r, g, bl;
    if (t <= 0)
        return a;
    if (t >= 16)
        return b;
    r = ((a >> 11) * (16 - t) + (b >> 11) * t) / 16;
    g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) / 16;
    bl = ((a & 31) * (16 - t) + (b & 31) * t) / 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
/* each biome's ground, over the season's: sand in the desert, pine dark up high, the isles' bright greens */
static void biome_tint(void)
{
    int k;
    switch (C.biome) {
    case B_DESERT: {
        static const uint16_t SAND[5] = {RGB(214, 186, 132), RGB(222, 196, 142), RGB(229, 205, 152), RGB(214, 178, 122), RGB(200, 160, 104)};
        for (k = 0; k < 5; k++)
            SEAS[SC_G0 + k] = lerp565(SEAS[SC_G0 + k], SAND[k], 0.88f);
        SEAS[SC_YARD] = lerp565(SEAS[SC_YARD], RGB(206, 186, 138), 0.75f);
        SEAS[SC_LEAF] = lerp565(SEAS[SC_LEAF], RGB(104, 140, 72), 0.6f);
        SEAS[SC_LEAFMID] = lerp565(SEAS[SC_LEAFMID], RGB(120, 150, 80), 0.6f);
        SEAS[SC_RIVER] = RGB(62, 140, 160);
        break;
    }
    case B_ALPINE:
        for (k = 0; k < 5; k++)
            SEAS[SC_G0 + k] = lerp565(SEAS[SC_G0 + k], RGB(104, 150, 116), 0.3f);
        SEAS[SC_FOREST] = lerp565(SEAS[SC_FOREST], RGB(40, 82, 58), 0.6f);
        SEAS[SC_LEAF] = lerp565(SEAS[SC_LEAF], RGB(40, 96, 62), 0.6f);
        SEAS[SC_LEAF2] = lerp565(SEAS[SC_LEAF2], RGB(30, 72, 48), 0.6f);
        SEAS[SC_RIVER] = RGB(84, 150, 196);
        break;
    case B_VOLCANO:
        for (k = 0; k < 5; k++)
            SEAS[SC_G0 + k] = lerp565(SEAS[SC_G0 + k], RGB(84, 170, 72), 0.35f);
        SEAS[SC_FOREST] = lerp565(SEAS[SC_FOREST], RGB(36, 112, 52), 0.5f);
        SEAS[SC_LEAF] = lerp565(SEAS[SC_LEAF], RGB(52, 150, 62), 0.4f);
        break;
    case B_PLAINS:
        for (k = 0; k < 5; k++)
            SEAS[SC_G0 + k] = lerp565(SEAS[SC_G0 + k], RGB(196, 184, 102), season.vis == 1 || season.vis == 2 ? 0.3f : 0.12f);
        break;
    case B_BEACH:
        for (k = 0; k < 5; k++)
            SEAS[SC_G0 + k] = lerp565(SEAS[SC_G0 + k], RGB(176, 196, 132), 0.2f);
        break;
    case B_RIVER:
        for (k = 0; k < 5; k++)
            SEAS[SC_G0 + k] = lerp565(SEAS[SC_G0 + k], RGB(92, 168, 88), 0.2f);
        SEAS[SC_FOREST] = lerp565(SEAS[SC_FOREST], RGB(52, 120, 64), 0.3f);
        break;
    default:                                     /* forest: deeper woods */
        SEAS[SC_FOREST] = lerp565(SEAS[SC_FOREST], RGB(44, 96, 50), 0.35f);
        break;
    }
}
static void season_update(void)
{
    int idx, nb, k, sp = proj.par[P_SEASON];
    float frac, sk;
    if (sp == 0) {                               /* cycle: a few days each */
        float f = tm.days / (float)(proj.par[P_SEASONDAYS] < 1 ? 1 : proj.par[P_SEASONDAYS]) + 1.05f;
        f -= 4.0f * fm_floorf(f * 0.25f);
        idx = (int)f;
        frac = f - (float)idx;
    } else if (sp == 1) {                        /* the growth's calendar: a season every thirteen weeks */
        float f = (float)(((int)(C.tick % 52u) - 9 + 52) % 52) / 13.0f;
        idx = (int)f;
        frac = f - (float)idx;
    } else {
        idx = sp - 2;
        frac = 0.0f;
    }
    idx &= 3;
    nb = (idx + 1) & 3;
    sk = fm_clampf((frac - 0.82f) / 0.18f, 0.0f, 1.0f);
    sk = sk * sk * (3.0f - 2.0f * sk);
    season.idx = (uint8_t)(sk > 0.5f ? nb : idx);
    {   /* the warm lands never whiten: the isles and the beach keep summer through winter, the desert autumn */
        static const uint8_t WARM[NBIOME][4] = {{0, 1, 2, 3}, {0, 1, 2, 2}, {0, 1, 2, 3}, {0, 1, 1, 1}, {0, 1, 2, 3}, {0, 1, 2, 2}, {0, 1, 2, 3}};
        int b = C.biome < NBIOME ? C.biome : 0;
        idx = WARM[b][idx];
        nb = WARM[b][nb];
    }
    for (k = 0; k < SC_N; k++)
        SEAS[k] = lerp565(SEASON_PAL[idx][k], SEASON_PAL[nb][k], sk);
    season.vis = (uint8_t)(sk > 0.5f ? nb : idx);
    season.snow = (idx == 3 ? 1.0f - sk : 0.0f) + (nb == 3 ? sk : 0.0f);
    biome_tint();
}

/* ------------------------------------------------------------ weather --- */
enum { W_CLEAR, W_CLOUDY, W_RAIN, W_STORM, W_FOG, W_SNOW, W_N };
static const char *const W_NAME[W_N] = {"clear", "cloudy", "rain", "storm", "fog", "snow"};
static struct {
    uint8_t now, prev;
    float blend;                        /* 0..1: from prev to now, over six seconds */
    uint32_t next, fade_t;
} wx = {W_CLEAR, W_CLEAR, 1.0f, 0, 0};
static float wlvl(int w) { return (wx.now == w ? wx.blend : 0.0f) + (wx.prev == w && wx.prev != wx.now ? 1.0f - wx.blend : 0.0f); }
static float wet(void) { return fm_minf(1.0f, wlvl(W_RAIN) + wlvl(W_STORM)); }
static void weather_set(int w)
{
    if (w == wx.now)
        return;
    wx.prev = wx.now;
    wx.now = (uint8_t)w;
    wx.blend = 0.0f;
    wx.fade_t = plat_ms();
}
static int weather_pick(void)
{
    static const uint8_t TR[W_N][W_N] = {   /* tenths: from each weather to the next */
        {40, 40, 0, 0, 10, 0}, {35, 20, 35, 0, 10, 0}, {5, 40, 25, 30, 0, 0}, {0, 40, 60, 0, 0, 0}, {50, 50, 0, 0, 0, 0}, {20, 50, 0, 0, 0, 30}};
    static const uint8_t SW[4][W_N] = {     /* the season's leaning, tenths */
        {10, 10, 15, 8, 13, 10}, {16, 10, 7, 15, 4, 10}, {8, 15, 13, 10, 16, 10}, {8, 13, 10, 10, 12, 16}};
    static const uint8_t BW[NBIOME][W_N] = {   /* the biome's, tenths: clear, cloud, rain, storm, fog, snow */
        {10, 10, 13, 10, 14, 10}, {24, 10, 3, 9, 9, 3}, {9, 11, 8, 11, 12, 18}, {12, 10, 15, 10, 7, 5},
        {14, 10, 10, 12, 8, 10}, {17, 10, 10, 9, 12, 3}, {10, 11, 15, 10, 13, 9}};
    int w, tot = 0, r, from = wx.now, nx = W_CLEAR, b = C.biome < NBIOME ? C.biome : 0;
    for (w = 0; w < W_N; w++)
        tot += TR[from][w] * SW[season.idx][w] * BW[b][w];
    r = lirnd(tot);
    for (w = 0; w < W_N; w++) {
        r -= TR[from][w] * SW[season.idx][w] * BW[b][w];
        if (r < 0) {
            nx = w;
            break;
        }
    }
    if (b == B_VOLCANO)                          /* the isles' "snow" is ash, any time of year */
        return nx;
    if (b == B_ALPINE && season.idx != 1) {      /* the mountains: snow but in summer */
        if (season.idx == 3 && nx == W_RAIN)
            nx = W_SNOW;
        return nx;
    }
    if (season.idx == 3 && (nx == W_RAIN || nx == W_STORM) && b != B_DESERT && b != B_BEACH)
        nx = W_SNOW;
    if (season.idx != 3 && nx == W_SNOW)
        nx = W_RAIN;
    return nx;
}
/* each biome's look of the six weathers: the storm a thunderstorm, a dust storm, a blizzard, a typhoon; the fog
 * mist, heat haze, ash; the snow snow, frost, ash fall, sleet */
enum { LK_CLEAR, LK_CLOUD, LK_RAIN, LK_STORM, LK_DUST, LK_BLIZZARD, LK_TYPHOON, LK_FOG, LK_HAZE, LK_ASHHAZE, LK_SNOW, LK_FROST, LK_ASH,
       LK_SLEET };
static int look_of(int w)
{
    static const uint8_t L[NBIOME][W_N] = {
        {LK_CLEAR, LK_CLOUD, LK_RAIN, LK_STORM, LK_FOG, LK_SNOW},      {LK_CLEAR, LK_CLOUD, LK_RAIN, LK_DUST, LK_HAZE, LK_FROST},
        {LK_CLEAR, LK_CLOUD, LK_RAIN, LK_BLIZZARD, LK_FOG, LK_SNOW},   {LK_CLEAR, LK_CLOUD, LK_RAIN, LK_TYPHOON, LK_ASHHAZE, LK_ASH},
        {LK_CLEAR, LK_CLOUD, LK_RAIN, LK_STORM, LK_FOG, LK_SNOW},      {LK_CLEAR, LK_CLOUD, LK_RAIN, LK_TYPHOON, LK_FOG, LK_SLEET},
        {LK_CLEAR, LK_CLOUD, LK_RAIN, LK_STORM, LK_FOG, LK_SNOW}};
    return L[C.biome < NBIOME ? C.biome : 0][(unsigned)w < W_N ? w : 0];
}
static float llvl(int look)                     /* how much of a look the sky is now */
{
    float v = 0.0f;
    int w;
    for (w = 0; w < W_N; w++)
        if (look_of(w) == look)
            v += wlvl(w);
    return v;
}
/* lightning: only out of rain, a rainstorm, or snow (a thundersnow); not out of dust or ash */
static float lightning_sky(void)
{
    float v = 0.0f;
    int w;
    for (w = W_RAIN; w <= W_SNOW; w++) {
        int lk = look_of(w);
        if (w == W_FOG || lk == LK_DUST || lk == LK_ASH || lk == LK_FROST)
            continue;
        v += wlvl(w) * (w == W_STORM ? 1.0f : w == W_RAIN ? 0.35f : 0.2f);
    }
    return v;
}
/* the day's rhythm: rush hours, quiet nights, fewer out in the rain */
static float rush(void)
{
    float p = phase(), a = (p - 0.33f) / 0.05f, b = (p - 0.72f) / 0.05f;
    return (0.3f + 0.7f * day_lit()) * (0.75f + 0.35f * fm_minf(1.0f, fm_expf(-a * a) + fm_expf(-b * b))) * (1.0f - 0.25f * wet());
}
static float walkers(void) { return fm_maxf(0.08f, (0.15f + 0.85f * day_lit()) * (1.0f - 0.6f * wet() - 0.3f * wlvl(W_SNOW))); }

/* ------------------------------------------------------------- routes --- */
static uint8_t r_seen[MN] EN_POOL;
static uint16_t r_q[4096] EN_POOL;
static uint8_t r_stamp;
#define NPATH 128
/* the shortest way along the roads from a to b (the beltway too, unless on foot): its tiles into out, how many
 * (0: none, or shorter than 3); a longer way is cut at max */
static int find_path(int a, int b, int foot, uint16_t *out, int max)
{
    int h = 0, t = 0, n = 0, i, d, k;
    static const int DD[4] = {1, -1, MW, -MW};
    if (a < 0 || b < 0 || a == b)
        return 0;
    if (++r_stamp == 0) {
        memset(r_seen, 0, MN);
        r_stamp = 1;
    }
    r_q[t++] = (uint16_t)a;
    r_seen[a] = r_stamp;
    c_scr[a] = 0xFFFFu;
    d = lirnd(4);
    while (h < t) {
        i = r_q[h++];
        if (i == b)
            break;
        for (k = 0; k < 4; k++) {
            int dd = DD[(d + k) & 3], j = i + dd;
            if (j < 0 || j >= MN || ((dd == 1 || dd == -1) && j / MW != i / MW))
                continue;
            if (!c_road[j] || r_seen[j] == r_stamp)
                continue;
            if (foot && c_plan[j] == PL_BELTWAY && j != b)
                continue;
            r_seen[j] = r_stamp;
            c_scr[j] = (uint16_t)i;
            if (t < 4096)
                r_q[t++] = (uint16_t)j;
        }
    }
    if (r_seen[b] != r_stamp)
        return 0;
    for (i = b; i != 0xFFFF && n < 4096; i = c_scr[i])
        n++;
    if (n < 3)
        return 0;
    {   /* the way, from a; a longer one is cut after max tiles */
        int m = n > max ? max : n, c2 = 0;
        for (i = b; i != 0xFFFF; i = c_scr[i], c2++) {
            int pos = n - 1 - c2;
            if (pos < m)
                out[pos] = (uint16_t)i;
        }
        return m;
    }
}
/* the road beside a building (on foot, not the beltway) */
static int road_near(const st_t *s, int foot)
{
    int n = s->size, x, y, best = -1;
    float cx = (float)s->x + (float)(n - 1) * 0.5f, cy = (float)s->y + (float)(n - 1) * 0.5f, bd = 1e9f;
    for (y = s->y - 2; y < s->y + n + 2; y++)
        for (x = s->x - 2; x < s->x + n + 2; x++) {
            int i;
            float d;
            if (!in_map(x, y))
                continue;
            i = y * MW + x;
            if (!c_road[i] || (foot && c_plan[i] == PL_BELTWAY))
                continue;
            d = fm_fabsf((float)x - cx) + fm_fabsf((float)y - cy) + lrnd() * 0.5f;
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
    return best;
}
/* a building of a type (BT_CIV: a landmark) in r (r null: anywhere); 0 none */
static int pick_st(int type, const rect_t *r)
{
    int k, n, cnt = 0, pick, si;
    if (type == BT_CIV) {
        n = C.nciv;
        for (k = 0; k < n; k++) {
            const st_t *s = &c_st[c_civ[k]];
            if (!r || in_rect(r, s->x, s->y))
                cnt++;
        }
        if (!cnt)
            return 0;
        pick = lirnd(cnt);
        for (k = 0; k < n; k++) {
            const st_t *s = &c_st[c_civ[k]];
            if ((!r || in_rect(r, s->x, s->y)) && pick-- == 0)
                return c_civ[k];
        }
        return 0;
    }
    n = C.nbl[type];
    for (k = 0; k < n; k++) {
        const st_t *s = &c_st[c_ref[c_bl[type][k]]];
        if (!r || in_rect(r, s->x, s->y))
            cnt++;
    }
    if (!cnt)
        return 0;
    pick = lirnd(cnt);
    for (k = 0; k < n; k++) {
        si = c_ref[c_bl[type][k]];
        if ((!r || in_rect(r, c_st[si].x, c_st[si].y)) && pick-- == 0)
            return (c_st[si].flags & SF_DEAD) ? 0 : si;
    }
    return 0;
}

/* --------------------------------------------------------------- cars --- */
enum { VK_CAR, VK_TRUCK, VK_BUS, VK_FIRE, VK_POLICE, VK_AMB };
static const uint16_t CARCOL[10] = {RGB(217, 71, 63), RGB(240, 240, 234), RGB(63, 143, 208), RGB(34, 38, 43), RGB(95, 160, 74),
                                    RGB(185, 190, 196), RGB(122, 79, 192), RGB(224, 123, 69), RGB(240, 240, 234), RGB(217, 71, 63)};
#define NCAR 64
typedef struct {
    uint8_t len, k, kind, col, on, keep, duty;  /* duty: answering an incident */
    float d, v;
    uint32_t arrived;
    uint16_t path[NPATH];               /* last: a new car clears what's before it */
} car_t;
static car_t cars[NCAR] EN_POOL;
static int ncars;
static uint32_t traffic_t;
static int solo_car = -1;                       /* the camera's car in the idle view: its route plays the lead */
static int game_car = -1, game_kind = -1;      /* the secret game's: the player drives it (game.c); its kind isn't dispatched */

static void car_pos(const car_t *c, float *x, float *y, int *dx, int *dy)
{
    int i0 = c->path[c->k], i1 = c->path[c->k + 1 < c->len ? c->k + 1 : c->len - 1];
    int x0 = i0 % MW, y0 = i0 / MW, x1 = i1 % MW, y1 = i1 / MW, ddx = x1 - x0, ddy = y1 - y0;
    float ox = (float)-ddy * 0.22f, oy = (float)ddx * 0.22f;
    if (c->k > 0) {
        int ip = c->path[c->k - 1], pdx = x0 - ip % MW, pdy = y0 - ip / MW;
        float f = fm_minf(1.0f, c->d / 0.4f);
        ox = (float)-pdy * 0.22f * (1.0f - f) + ox * f;
        oy = (float)pdx * 0.22f * (1.0f - f) + oy * f;
    }
    *x = (float)x0 + 0.5f + (float)ddx * c->d + ox;
    *y = (float)y0 + 0.5f + (float)ddy * c->d + oy;
    if (dx) {
        *dx = ddx;
        *dy = ddy;
    }
}
static car_t *car_new(void)
{
    int k;
    for (k = 0; k < NCAR; k++)
        if (!cars[k].on) {
            memset(&cars[k], 0, (uint32_t)((char *)cars[k].path - (char *)&cars[k]));
            cars[k].on = 1;
            return &cars[k];
        }
    return 0;
}
static void car_home(car_t *c)                  /* an emergency done: back the way it came */
{
    int a = 0, b = c->len - 1;
    if (!c || !c->on)
        return;
    while (a < b) {
        uint16_t t = c->path[a];
        c->path[a++] = c->path[b];
        c->path[b--] = t;
    }
    c->k = 0;
    c->d = 0.0f;
    c->arrived = 0;
    c->duty = 0;
    c->keep = 0;
}
static void spawn_car(void)
{
    rect_t near = view_rect(3.0f);
    int o = pick_st(BT_RES, &near), d = 0, t, a, b, n;
    float r = lrnd();
    car_t *c;
    const st_t *ds;
    if (!o)
        o = pick_st(BT_RES, 0);
    if (!o)
        return;
    {
        rect_t local = {c_st[o].x - 18.0f, c_st[o].y - 18.0f, c_st[o].x + 18.0f, c_st[o].y + 18.0f};
        t = r < 0.4f ? BT_COM : r < 0.65f ? BT_IND : r < 0.82f ? BT_CIV : BT_RES;
        d = pick_st(t, lrnd() < 0.5f ? &near : &local);
        if (!d)
            d = pick_st(BT_RES, &local);
    }
    if (!d || d == o)
        return;
    a = road_near(&c_st[o], 0);
    b = road_near(&c_st[d], 0);
    if (lrnd() < 0.5f) {
        int s = a;
        a = b;
        b = s;
    }
    c = car_new();
    if (!c)
        return;
    n = find_path(a, b, 0, c->path, NPATH);
    if (!n) {
        c->on = 0;
        return;
    }
    c->len = (uint8_t)n;
    ds = &c_st[d];
    c->kind = VK_CAR;
    if (ds->type == BT_IND && lrnd() < 0.55f)
        c->kind = VK_TRUCK;
    else if (C.stage >= 1 && lrnd() < 0.05f)
        c->kind = VK_BUS;
    c->v = (c->kind == VK_CAR ? 2.3f : c->kind == VK_TRUCK ? 1.6f : 1.5f) * (0.8f + lrnd() * 0.4f);
    c->col = (uint8_t)lirnd(10);
}
static int traffic_target(void)
{
    rect_t r = view_rect(0.0f);
    int x, y, n = 0;
    float dens = 0.1f + 0.14f * fm_minf(1.0f, (float)C.pop / 40000.0f), v;
    for (y = (int)fm_maxf(0.0f, r.y0); y <= (int)fm_minf((float)(MH - 1), r.y1); y++)
        for (x = (int)fm_maxf(0.0f, r.x0); x <= (int)fm_minf((float)(MW - 1), r.x1); x++)
            n += c_road[y * MW + x] != 0;
    v = (float)n * dens * rush() * (float)proj.par[P_TRAFFIC] * 0.1f;
    return v > 56.0f ? 56 : (int)v;
}

/* the music: a car rolls into a tile on screen */
static uint8_t music_on(void) { return en_playing; }
static void car_tile(car_t *c, int is_solo)
{
    int i = c->path[c->k], x = i % MW, y = i / MW, j, horiz, k;
    rect_t vr = view_rect(0.0f);
    float dens = (float)proj.par[P_DENSITY] * 0.01f;
    if (!music_on() || !in_rect(&vr, (float)x + 0.5f, (float)y + 0.5f))
        return;
    if (is_solo) {                               /* the followed car sings its way: each block a note */
        static int deg;
        if (lrnd() < 0.55f) {
            deg += lirnd(5) - 2;
            deg = deg < -3 ? -3 : deg > 9 ? 9 : deg;
            en_event(E_SOLO, deg + 3, 0);
        }
    }
    j = c->k + 1 < c->len ? c->path[c->k + 1] : i;
    horiz = j - i == 1 || i - j == 1 || j == i;
    if (lrnd() < dens * 0.7f) {
        if (c_road[i] == 2)
            en_event(E_TUNK, 0, 0);
        else if (c_plan[i] != PL_BELTWAY && LANE(i) == 0)
            en_event((c_fl2[i] & F2_ROUND) ? E_TOM : c_plan[i] == PL_AVENUE ? E_SNARE : E_HAT, 0, 0);
    }
    if (lrnd() > dens)
        return;
    for (k = 0; k < 2; k++) {
        int side = (k == 0) == (lrnd() < 0.5f) ? 1 : -1, jj = horiz ? i + side * MW : i + side, si;
        const st_t *s;
        if (jj < 0 || jj >= MN || !(si = c_ref[jj]))
            continue;
        s = &c_st[si];
        if (s->flags & SF_DEAD)
            continue;
        if (s->type == BT_RES)
            en_event(E_RES, s->L, 0);
        else if (s->type == BT_COM)
            en_event(E_COM, s->L, (s->size > 1) << 7 | (s->x * 3 + s->y * 5) % 7);
        else if (s->type == BT_IND)
            en_event(E_IND, 0, 0);
        else if (s->kind < CV_GREEN0) {
            if ((int32_t)(plat_ms() - c_st[si].motif) >= 0) {
                c_st[si].motif = plat_ms() + 8000u;
                en_event(E_CIVIC, s->kind, 0);
            }
        } else
            en_event(E_GREEN, s->x + s->y, 0);
        break;
    }
}
static void cars_update(float dt)
{
    rect_t keep = view_rect(8.0f);
    int k;
    ncars = 0;
    for (k = 0; k < NCAR; k++) {
        car_t *c = &cars[k];
        float x, y, sp;
        if (!c->on)
            continue;
        if (k == game_car) {                     /* the player drives this one */
            ncars++;
            continue;
        }
        sp = c->v * (c_plan[c->path[c->k]] == PL_BELTWAY ? 1.8f : 1.0f);
        c->d += sp * dt;
        while (c->d >= 1.0f && c->k < c->len - 1) {
            c->d -= 1.0f;
            c->k++;
            car_tile(c, k == solo_car);
        }
        if (c->k >= c->len - 1) {
            if (c->duty) {                       /* there: it waits for the incident */
                c->d = 0.0f;
                if (!c->arrived)
                    c->arrived = plat_ms() | 1u;
            } else {
                c->on = 0;
                continue;
            }
        }
        car_pos(c, &x, &y, 0, 0);
        if (!c->keep && !in_rect(&keep, x, y)) {
            c->on = 0;
            continue;
        }
        ncars++;
    }
    if (plat_ms() - traffic_t >= 120u) {
        int target = traffic_target(), n;
        traffic_t = plat_ms();
        for (n = 0; n < 6 && ncars < target; n++) {
            spawn_car();
            ncars++;
        }
    }
}

/* -------------------------------------------------------------- people --- */
static const uint16_t SHIRT[10] = {RGB(217, 71, 63), RGB(63, 143, 208), RGB(240, 240, 234), RGB(232, 185, 49), RGB(95, 160, 74),
                                   RGB(122, 79, 192), RGB(224, 123, 69), RGB(34, 38, 43), RGB(201, 90, 138), RGB(42, 161, 152)};
static const uint16_t SKIN[5] = {RGB(241, 201, 165), RGB(217, 163, 122), RGB(168, 112, 74), RGB(122, 74, 46), RGB(232, 184, 150)};
/* moodlets: 5 x 5 icons */
enum { I_HEART, I_SMILE, I_NOTE, I_SUN, I_COFFEE, I_FROWN, I_EXCLAIM, I_QUESTION, I_ZZZ, I_STAR, I_SWEAT, I_LEAF, I_MONEY, I_N };
static const struct { uint16_t c; uint8_t rows[5]; } ICON[I_N] = {
    {RGB(217, 71, 63), {0x0A, 0x1F, 0x1F, 0x0E, 0x04}}, {RGB(199, 149, 42), {0x00, 0x0A, 0x00, 0x11, 0x0E}},
    {RGB(63, 143, 208), {0x06, 0x05, 0x04, 0x0C, 0x0C}}, {RGB(224, 169, 42), {0x15, 0x0E, 0x1F, 0x0E, 0x15}},
    {RGB(138, 90, 58), {0x0A, 0x00, 0x1E, 0x1F, 0x0E}}, {RGB(107, 110, 117), {0x00, 0x0A, 0x00, 0x0E, 0x11}},
    {RGB(217, 71, 63), {0x04, 0x04, 0x04, 0x00, 0x04}}, {RGB(63, 143, 208), {0x0E, 0x02, 0x04, 0x00, 0x04}},
    {RGB(122, 79, 192), {0x1E, 0x04, 0x08, 0x1E, 0x00}}, {RGB(224, 169, 42), {0x04, 0x0E, 0x1F, 0x0E, 0x0A}},
    {RGB(79, 159, 208), {0x04, 0x0E, 0x1F, 0x1F, 0x0E}}, {RGB(79, 154, 74), {0x03, 0x07, 0x0E, 0x1C, 0x10}},
    {RGB(60, 154, 95), {0x0E, 0x14, 0x0E, 0x05, 0x0E}}};
#define NPED 64
#define PPATH 40
typedef struct {
    uint8_t len, k, on, shirt, skin, lastbub, nicons, first;
    int8_t side;
    uint16_t park;                      /* strolling in this green space (a building slot), or 0 */
    float d, v, x, y, tx, ty;
    uint32_t age, life, cool;
    int32_t pause;
    uint32_t c_start, c_until;          /* a conversation */
    uint16_t c_turn;
    uint8_t icons[6];
    uint16_t path[PPATH];               /* last, as the cars' */
} ped_t;
static ped_t peds[NPED] EN_POOL;
static int npeds;
static uint32_t ped_t0, meet_t;
static void ped_pos(const ped_t *p, float *x, float *y)
{
    int i0, i1, x0, y0, dx, dy;
    float o, ox, oy;
    if (p->park) {
        *x = p->x;
        *y = p->y;
        return;
    }
    i0 = p->path[p->k];
    i1 = p->path[p->k + 1 < p->len ? p->k + 1 : p->len - 1];
    x0 = i0 % MW;
    y0 = i0 / MW;
    dx = i1 % MW - x0;
    dy = i1 / MW - y0;
    o = 0.41f * (float)p->side;
    ox = (float)-dy * o;
    oy = (float)dx * o;
    if (p->k > 0) {
        int ip = p->path[p->k - 1], pdx = x0 - ip % MW, pdy = y0 - ip / MW;
        float f = fm_minf(1.0f, p->d / 0.5f);
        ox = (float)-pdy * o * (1.0f - f) + ox * f;
        oy = (float)pdx * o * (1.0f - f) + oy * f;
    }
    *x = (float)x0 + 0.5f + (float)dx * p->d + ox;
    *y = (float)y0 + 0.5f + (float)dy * p->d + oy;
}
static void park_target(ped_t *p)
{
    const st_t *c = &c_st[p->park];
    float n = (float)c->size;
    p->tx = (float)c->x + 0.2f + lrnd() * (n - 0.4f);
    p->ty = (float)c->y + 0.2f + lrnd() * (n - 0.4f);
    p->pause = lrnd() < 0.3f ? (int32_t)(1500.0f + lrnd() * 3000.0f) : 0;
}
static ped_t *ped_new(void)
{
    int k;
    for (k = 0; k < NPED; k++)
        if (!peds[k].on) {
            memset(&peds[k], 0, (uint32_t)((char *)peds[k].path - (char *)&peds[k]));
            peds[k].on = 1;
            peds[k].shirt = (uint8_t)lirnd(10);
            peds[k].skin = (uint8_t)lirnd(5);
            return &peds[k];
        }
    return 0;
}
static void spawn_ped(void)
{
    rect_t near = view_rect(2.0f);
    ped_t *p;
    if (lrnd() < 0.35f) {                        /* a stroll in a park in view */
        int k, cnt = 0, pick;
        for (k = 0; k < C.nciv; k++) {
            const st_t *s = &c_st[c_civ[k]];
            if (civ_green(s->kind) && s->kind != CV_MARINA && s->kind != CV_CEMETERY && in_rect(&near, s->x, s->y))
                cnt++;
        }
        if (cnt && (p = ped_new()) != 0) {
            pick = lirnd(cnt);
            for (k = 0; k < C.nciv; k++) {
                const st_t *s = &c_st[c_civ[k]];
                if (civ_green(s->kind) && s->kind != CV_MARINA && s->kind != CV_CEMETERY && in_rect(&near, s->x, s->y) && pick-- == 0) {
                    p->park = c_civ[k];
                    break;
                }
            }
            {
                const st_t *s = &c_st[p->park];
                p->x = (float)s->x + 0.2f + lrnd() * ((float)s->size - 0.4f);
                p->y = (float)s->y + 0.2f + lrnd() * ((float)s->size - 0.4f);
            }
            p->v = 0.22f + lrnd() * 0.2f;
            p->life = (uint32_t)(12000.0f + lrnd() * 18000.0f);
            park_target(p);
            return;
        }
    }
    {   /* an errand: a few blocks, home to the shops, a school, a friend's */
        int o = lrnd() < 0.5f ? pick_st(BT_RES, &near) : pick_st(BT_COM, &near), d, a, b, n;
        float q = lrnd();
        rect_t r;
        if (!o)
            return;
        r.x0 = c_st[o].x - 10.0f;
        r.y0 = c_st[o].y - 10.0f;
        r.x1 = c_st[o].x + 10.0f;
        r.y1 = c_st[o].y + 10.0f;
        d = pick_st(q < 0.45f ? BT_COM : q < 0.75f ? BT_CIV : BT_RES, &r);
        if (!d || d == o)
            return;
        a = road_near(&c_st[o], 1);
        b = road_near(&c_st[d], 1);
        if (lrnd() < 0.5f) {
            int s = a;
            a = b;
            b = s;
        }
        p = ped_new();
        if (!p)
            return;
        n = find_path(a, b, 1, p->path, PPATH);
        if (!n) {
            p->on = 0;
            return;
        }
        p->len = (uint8_t)n;
        p->v = 0.45f + lrnd() * 0.25f;
        p->side = (int8_t)(lrnd() < 0.5f ? 1 : -1);
    }
}
static int ped_target(void)
{
    rect_t r = view_rect(0.0f);
    int x, y, n = 0, g = 0;
    float dens = 0.06f + 0.14f * fm_minf(1.0f, (float)C.pop / 40000.0f), v;
    for (y = (int)fm_maxf(0.0f, r.y0); y <= (int)fm_minf((float)(MH - 1), r.y1); y++)
        for (x = (int)fm_maxf(0.0f, r.x0); x <= (int)fm_minf((float)(MW - 1), r.x1); x++) {
            int i = y * MW + x;
            if (c_road[i])
                n++;
            else if (c_bt[i] == BT_CIV && civ_green(c_st[c_ref[i]].kind))
                g++;
        }
    v = ((float)n * dens + (float)g * 0.6f) * walkers() * (float)proj.par[P_PEOPLE] * 0.1f;
    return cam.ts < 6 ? 0 : v > 60.0f ? 60 : (int)v;
}
/* what they talk about: where they are, the hour, the sky */
static int mood_icon(float x, float y)
{
    uint8_t pool[24];
    int n = 0, xi = (int)fm_clampf(x, 0.0f, MW - 1.0f), yi = (int)fm_clampf(y, 0.0f, MH - 1.0f), dx, dy, nI = 0, nC = 0, nP = 0;
    float lv = LVF(yi * MW + xi);
    for (dy = -3; dy <= 3; dy++)
        for (dx = -3; dx <= 3; dx++) {
            int xx = xi + dx, yy = yi + dy, b;
            if (!in_map(xx, yy))
                continue;
            b = c_bt[yy * MW + xx];
            if (b == BT_IND) nI++;
            else if (b == BT_COM) nC++;
            else if (b == BT_CIV && civ_green(c_st[c_ref[yy * MW + xx]].kind)) nP++;
        }
#define ADD(i) (pool[n++] = (uint8_t)(i))
    ADD(I_SMILE); ADD(I_SMILE); ADD(I_QUESTION); ADD(I_NOTE); ADD(I_STAR);
    if (nP) { ADD(I_HEART); ADD(I_HEART); ADD(I_SUN); ADD(I_LEAF); }
    if (nI >= 2) { ADD(I_FROWN); ADD(I_SWEAT); ADD(I_FROWN); }
    if (lv > 0.55f) { ADD(I_COFFEE); ADD(I_COFFEE); ADD(I_EXCLAIM); }
    if (nC >= 3) { ADD(I_STAR); ADD(I_COFFEE); }
    if (C.dem_r < 0.0f) ADD(I_FROWN);
    if (day_lit() < 0.3f) { ADD(I_ZZZ); ADD(I_ZZZ); }
    if (wet() > 0.5f) { ADD(I_SWEAT); ADD(I_FROWN); }
    if (wlvl(W_SNOW) > 0.5f) { ADD(I_STAR); ADD(I_HEART); }
#undef ADD
    return pool[lirnd(n)];
}
static void find_meetings(void)
{
    uint32_t now = plat_ms();
    int a, b;
    if (!proj.par[P_CHATS])
        return;
    for (a = 0; a < NPED; a++) {
        ped_t *A = &peds[a];
        float ax, ay;
        if (!A->on || A->c_until > now || A->cool > now)
            continue;
        if (A->park ? (A->age < 800u || A->age + 6000u > A->life) : ((float)A->k + A->d < 0.6f || (float)(A->len - 1 - A->k) - A->d < 0.8f))
            continue;
        ped_pos(A, &ax, &ay);
        for (b = a + 1; b < NPED; b++) {
            ped_t *B = &peds[b];
            float bx, by;
            if (!B->on || B->c_until > now || B->cool > now)
                continue;
            if (B->park ? (B->age < 800u || B->age + 6000u > B->life) : ((float)B->k + B->d < 0.6f || (float)(B->len - 1 - B->k) - B->d < 0.8f))
                continue;
            ped_pos(B, &bx, &by);
            if (fm_fabsf(ax - bx) > 0.7f || fm_fabsf(ay - by) > 0.7f)
                continue;
            if (lrnd() > 0.45f) {
                A->cool = B->cool = now + 5000u;
                break;
            }
            {
                int n = 3 + lirnd(4), k;
                uint16_t turn = (uint16_t)(1000 + lirnd(300));
                A->c_start = B->c_start = now + 250u;
                A->c_until = B->c_until = now + 250u + (uint32_t)n * turn + 400u;
                A->c_turn = B->c_turn = turn;
                A->nicons = B->nicons = (uint8_t)n;
                for (k = 0; k < n; k++)
                    A->icons[k] = B->icons[k] = (uint8_t)mood_icon((ax + bx) * 0.5f, (ay + by) * 0.5f);
                A->first = 1;
                B->first = 0;
                A->lastbub = B->lastbub = 255;
            }
            break;
        }
    }
}
static void peds_update(float dt, uint32_t dms)
{
    rect_t keep = view_rect(6.0f);
    uint32_t now = plat_ms();
    int k;
    npeds = 0;
    for (k = 0; k < NPED; k++) {
        ped_t *p = &peds[k];
        float x, y;
        if (!p->on)
            continue;
        if (p->c_until > now) {
            if (p->park)
                p->age += dms * 3u / 10u;
            if (p->c_until <= now + dms)
                p->cool = now + 15000u + (uint32_t)lirnd(15000);
        } else if (p->park) {
            p->age += dms;
            if (p->pause > 0) {
                p->pause -= (int32_t)dms;
            } else {
                float dx = p->tx - p->x, dy = p->ty - p->y, d = lhyp(dx, dy), st = p->v * dt;
                if (d <= st) {
                    p->x = p->tx;
                    p->y = p->ty;
                    park_target(p);
                } else {
                    p->x += dx / d * st;
                    p->y += dy / d * st;
                }
            }
        } else {
            p->d += p->v * dt;
            while (p->d >= 1.0f && p->k < p->len - 1) {
                p->d -= 1.0f;
                p->k++;
            }
        }
        if (p->park) {
            const st_t *s = &c_st[p->park];
            if ((s->flags & SF_DEAD) || s->type != BT_CIV || p->age > p->life) {
                p->on = 0;
                continue;
            }
        } else if (p->k >= p->len - 1) {
            p->on = 0;
            continue;
        }
        ped_pos(p, &x, &y);
        if (!in_rect(&keep, x, y)) {
            p->on = 0;
            continue;
        }
        npeds++;
    }
    if (now - ped_t0 >= 150u) {
        int target = ped_target(), n;
        ped_t0 = now;
        for (n = 0; n < 4 && npeds < target; n++) {
            spawn_ped();
            npeds++;
        }
    }
    if (now - meet_t >= 250u) {
        meet_t = now;
        find_meetings();
    }
}

/* -------------------------------------------------------------- birds --- */
#define NFLOCK 3
#define NBIRD 12
static struct {
    uint8_t on, gull, n;
    float x, y, vx, vy;
    uint32_t t0;
    float ox[NBIRD], oy[NBIRD], ph[NBIRD];
} flock[NFLOCK];
static uint32_t flock_next;
static void spawn_flock(void)
{
    rect_t r = view_rect(0.0f);
    int k, f, side = lirnd(4);
    float cx = (r.x0 + r.x1) * 0.5f, cy = (r.y0 + r.y1) * 0.5f, x, y, ang, sp, bx, by, lx, ly, d;
    for (f = 0; f < NFLOCK && flock[f].on; f++)
        ;
    if (f == NFLOCK)
        return;
    x = side == 0 ? r.x0 - 3.0f : side == 1 ? r.x1 + 3.0f : r.x0 + lrnd() * (r.x1 - r.x0);
    y = side == 2 ? r.y0 - 3.0f : side == 3 ? r.y1 + 3.0f : r.y0 + lrnd() * (r.y1 - r.y0);
    ang = atan2_t(cy - y + (lrnd() - 0.5f) * 8.0f, cx - x + (lrnd() - 0.5f) * 8.0f);
    sp = 2.2f + lrnd() * 1.3f;
    flock[f].on = 1;
    flock[f].x = x;
    flock[f].y = y;
    flock[f].vx = fm_cosf(ang) * sp;
    flock[f].vy = fm_sinf(ang) * sp;
    flock[f].gull = (uint8_t)(C.has_coast && lrnd() < 0.6f);
    flock[f].n = (uint8_t)(4 + lirnd(8));
    flock[f].t0 = plat_ms();
    bx = -fm_cosf(ang);
    by = -fm_sinf(ang);
    lx = -by;
    ly = bx;
    for (k = 0; k < flock[f].n; k++) {
        float row = (float)((k + 1) / 2), sd = k % 2 ? 1.0f : -1.0f;
        d = row * 0.7f;
        flock[f].ox[k] = bx * d + lx * sd * row * 0.55f + (lrnd() - 0.5f) * 0.3f;
        flock[f].oy[k] = by * d + ly * sd * row * 0.55f + (lrnd() - 0.5f) * 0.3f;
        flock[f].ph[k] = lrnd() * 2.0f;
    }
}

/* -------------------------------------------------------------- boats --- */
/* on the water, once the town has docks: rowboats and canoes on ponds, motorboats and barges on rivers and lakes,
 * sailboats, ferries and cargo ships at sea. A boat goes on along its water, turning where it must. */
enum { BK_ROW, BK_CANOE, BK_MOTOR, BK_BARGE, BK_SAIL, BK_FERRY, BK_CARGO };
#define NBOAT 8
static struct {
    uint8_t on, kind, col;
    int8_t dx, dy;
    float x, y, sp;                     /* the tile it's heading for is (x, y) + its direction, as it reaches each centre */
    uint32_t until;
} boats[NBOAT];
static uint32_t boat_next;
static const float BOAT_SP[7] = {0.4f, 0.55f, 1.4f, 0.5f, 0.8f, 0.9f, 0.45f};
static int water_of(int x, int y, int cls) { return in_map(x, y) && TERR(y * MW + x) == T_WATER && c_wb[y * MW + x] == cls; }
static void boat_turn(int b)
{
    static const int8_t D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int x = (int)boats[b].x, y = (int)boats[b].y, cls = c_wb[y * MW + x], d, opts[4], n = 0;
    if (water_of(x + boats[b].dx, y + boats[b].dy, cls) && lrnd() < 0.85f)
        return;                                  /* on, mostly */
    for (d = 0; d < 4; d++)
        if (water_of(x + D[d][0], y + D[d][1], cls) && !(D[d][0] == -boats[b].dx && D[d][1] == -boats[b].dy))
            opts[n++] = d;
    if (!n) {                                    /* a dead end: back */
        boats[b].dx = (int8_t)-boats[b].dx;
        boats[b].dy = (int8_t)-boats[b].dy;
        return;
    }
    d = opts[lirnd(n)];
    boats[b].dx = D[d][0];
    boats[b].dy = D[d][1];
}
static void spawn_boat(void)
{
    rect_t r = view_rect(4.0f);
    int b, tries, want = C.docks / 2 + (C.docks ? 1 : 0) + (C.has_coast && C.stage >= 1 ? 2 : 0);
    if (want > NBOAT)
        want = NBOAT;
    for (b = 0, tries = 0; b < NBOAT; b++)
        tries += boats[b].on;
    if (tries >= want)
        return;
    for (b = 0; b < NBOAT && boats[b].on; b++)
        ;
    for (tries = 0; tries < 30; tries++) {       /* a water tile near the view */
        int x = (int)(r.x0 + lrnd() * (r.x1 - r.x0)), y = (int)(r.y0 + lrnd() * (r.y1 - r.y0)), cls, k;
        if (!in_map(x, y) || TERR(y * MW + x) != T_WATER || !(cls = c_wb[y * MW + x]))
            continue;
        k = cls == 1 ? (lrnd() < 0.5f ? BK_ROW : BK_CANOE) : cls == 2 ? (lrnd() < 0.65f ? BK_MOTOR : BK_BARGE)
                                                                     : (lrnd() < 0.45f ? BK_SAIL : lrnd() < 0.5f ? BK_FERRY : BK_CARGO);
        boats[b].on = 1;
        boats[b].kind = (uint8_t)k;
        boats[b].col = (uint8_t)lirnd(10);
        boats[b].x = (float)x + 0.5f;
        boats[b].y = (float)y + 0.5f;
        boats[b].dx = 1;
        boats[b].dy = 0;
        boats[b].sp = BOAT_SP[k] * (0.8f + lrnd() * 0.4f);
        boats[b].until = plat_ms() + 60000u + (uint32_t)lirnd(60000);
        boat_turn(b);
        return;
    }
}
static void boats_update(float dt)
{
    int b;
    uint32_t now = plat_ms();
    if ((int32_t)(now - boat_next) >= 0) {
        boat_next = now + 4000u + (uint32_t)lirnd(5000);
        spawn_boat();
    }
    for (b = 0; b < NBOAT; b++) {
        float cx0, cy0, nx, ny;
        if (!boats[b].on)
            continue;
        if ((int32_t)(now - boats[b].until) >= 0) {
            boats[b].on = 0;
            continue;
        }
        cx0 = fm_floorf(boats[b].x) + 0.5f;
        cy0 = fm_floorf(boats[b].y) + 0.5f;
        nx = boats[b].x + (float)boats[b].dx * boats[b].sp * dt;
        ny = boats[b].y + (float)boats[b].dy * boats[b].sp * dt;
        if ((boats[b].dx > 0 && boats[b].x < cx0 && nx >= cx0) || (boats[b].dx < 0 && boats[b].x > cx0 && nx <= cx0) ||
            (boats[b].dy > 0 && boats[b].y < cy0 && ny >= cy0) || (boats[b].dy < 0 && boats[b].y > cy0 && ny <= cy0)) {
            boats[b].x = cx0;                    /* a tile's centre: on, or turn */
            boats[b].y = cy0;
            boat_turn(b);
        } else {
            boats[b].x = nx;
            boats[b].y = ny;
        }
    }
}

/* ---------------------------------------------------------- incidents --- */
#define NFIRE 12
static struct {
    uint8_t on;
    uint16_t si;
    uint32_t born, t0, quick, spread;
    int8_t truck;
    uint8_t spreads;                    /* how many it has lit (two at most: a fire grows, but not forever) */
    float x, y;
} fires[NFIRE];
static struct {
    uint8_t on, phase;                  /* 0 walking there, 1 robbing, 2 caught, 3 fleeing */
    uint16_t si;
    uint32_t born, t0, t1, t2, quick;
    int road;
    int8_t cop;
    ped_t robber;
} rob;
static struct {
    uint8_t on, phase;                  /* 0 waiting for help, 1 treated */
    int i;
    float x, y;
    uint32_t t0, t2, quick;
    int8_t amb, cop;
    uint8_t col[2];
} crash;
static struct {
    uint8_t on, phase;                  /* 0 arriving, 1 beaming, 2 leaving */
    float ux, uy, tx, ty, vx, vy, lift;
    uint32_t t0, t1, t2;
    uint8_t shirt, skin;
} ufo;
static struct {
    uint8_t on, n;
    uint32_t t0, dur;
    float ex, ey;
    struct { uint16_t si; uint32_t born, at; uint8_t done; } v[8];
} quake;
static struct { uint8_t on; float sx[8], sy[8]; uint32_t t0; } bolt;
static uint32_t flash_at, strike_next, thunder_at;
#define NDUST 48
static struct { float x, y, vx, vy, r; uint32_t t0; uint8_t on; } dust[NDUST];
static uint32_t demo_r;
#define NCLOUD 6
static struct { float x, y, r; } cloud[NCLOUD];
static float cloud_amt;

static const char *a_name(const st_t *s, char *b)  /* "A House" / "An Office block" */
{
    const char *n = st_name(s);
    int k = 0, j = 0, vowel = n[0] == 'A' || n[0] == 'E' || n[0] == 'I' || n[0] == 'O' || n[0] == 'U';
    b[k++] = 'A';
    if (vowel)
        b[k++] = 'N';
    b[k++] = ' ';
    while (n[j] && k < 22)
        b[k++] = n[j++];
    b[k] = 0;
    return b;
}
static int st_alive(int si, uint32_t born) { return si && c_st[si].type && !(c_st[si].flags & SF_DEAD) && c_st[si].born == born; }
/* the nearest station of a kind with a vehicle free sends it along the roads to tile `to`; -1 none (all out, or no
 * road there: the caller tries again). Each station keeps two vehicles. They always drive: nothing arrives by
 * itself. */
static int dispatch(int vkind, int civ, float x, float y, int to, uint32_t *quick)
{
    int k, n, out = 0, stations = 0, tries;
    uint16_t tried[3] = {0, 0, 0};
    car_t *c;
    if (quick)
        *quick = 0;
    if (to < 0 || vkind == game_kind)            /* the player answers these */
        return -1;
    for (k = 0; k < NCAR; k++)
        out += cars[k].on && cars[k].kind == vkind;
    for (k = 0; k < C.nciv; k++)
        stations += c_st[c_civ[k]].kind == civ;
    if (!stations || out >= 2 * stations)
        return -1;
    for (tries = 0; tries < 3; tries++) {       /* the nearest first; another if its roads don't reach */
        int best = 0, j;
        float bd = 1e9f;
        for (k = 0; k < C.nciv; k++) {
            const st_t *s = &c_st[c_civ[k]];
            float d = lhyp((float)s->x - x, (float)s->y - y);
            if (s->kind != civ || d >= bd)
                continue;
            for (j = 0; j < tries && tried[j] != c_civ[k]; j++)
                ;
            if (j < tries)
                continue;
            bd = d;
            best = c_civ[k];
        }
        if (!best)
            return -1;
        tried[tries] = (uint16_t)best;
        c = car_new();
        if (!c)
            return -1;
        n = find_path(road_near(&c_st[best], 1), to, 0, c->path, NPATH);
        if (!n) {
            c->on = 0;
            continue;
        }
        c->len = (uint8_t)n;
        c->kind = (uint8_t)vkind;
        c->v = 3.6f;
        c->keep = 1;
        c->duty = 1;
        return (int)(c - cars);
    }
    return -1;
}
static void ignite(int si)
{
    int f;
    st_t *s = &c_st[si];
    if ((s->flags & (SF_BURN | SF_DEAD)) || s->type == BT_CIV)
        return;
    for (f = 0; f < NFIRE && fires[f].on; f++)
        ;
    if (f == NFIRE)
        return;
    s->flags |= SF_BURN;
    fires[f].on = 1;
    fires[f].si = (uint16_t)si;
    fires[f].born = s->born;
    fires[f].t0 = plat_ms();
    fires[f].quick = 0;
    fires[f].spread = plat_ms() + 7000u;
    fires[f].spreads = 0;
    fires[f].x = (float)s->x + (float)s->size * 0.5f;
    fires[f].y = (float)s->y + (float)s->size * 0.5f;
    news(N_FIRE, s->type, s->L);
    fires[f].truck = (int8_t)dispatch(VK_FIRE, CV_FIRE, fires[f].x, fires[f].y, road_near(s, 1), 0);
    fires[f].quick = plat_ms() + 1500u;          /* (here: when to ask again for a truck) */
}
static int region_start(void);
static void strike(int hit)
{
    rect_t r = view_rect(-1.0f);
    float tx = r.x0 + lrnd() * (r.x1 - r.x0), ty = r.y0 + lrnd() * (r.y1 - r.y0), sx, sy;
    int target = 0, k;
    if (hit) {
        int cnt = 0, x, y, pick;
        for (y = (int)fm_maxf(0.0f, r.y0); y <= (int)fm_minf((float)(MH - 1), r.y1); y++)
            for (x = (int)fm_maxf(0.0f, r.x0); x <= (int)fm_minf((float)(MW - 1), r.x1); x++) {
                int si = c_ref[y * MW + x];
                if (si && c_st[si].type != BT_CIV && c_st[si].x == x && c_st[si].y == y && !(c_st[si].flags & SF_BURN))
                    cnt++;
            }
        if (cnt) {
            pick = lirnd(cnt);
            for (y = (int)fm_maxf(0.0f, r.y0); y <= (int)fm_minf((float)(MH - 1), r.y1) && !target; y++)
                for (x = (int)fm_maxf(0.0f, r.x0); x <= (int)fm_minf((float)(MW - 1), r.x1); x++) {
                    int si = c_ref[y * MW + x];
                    if (si && c_st[si].type != BT_CIV && c_st[si].x == x && c_st[si].y == y && !(c_st[si].flags & SF_BURN) && pick-- == 0) {
                        target = si;
                        break;
                    }
                }
        }
        if (target) {
            tx = (float)c_st[target].x + (float)c_st[target].size * 0.5f;
            ty = (float)c_st[target].y + (float)c_st[target].size * 0.5f;
        }
    }
    sx = tx + (lrnd() - 0.5f) * 6.0f;
    sy = ty - (float)cam.vh / (float)cam_ts() * 0.8f;
    for (k = 0; k < 8; k++) {
        float f = (float)k / 7.0f;
        bolt.sx[k] = sx + (tx - sx) * f + (k && k < 7 ? (lrnd() - 0.5f) * 1.6f : 0.0f);
        bolt.sy[k] = sy + (ty - sy) * f;
    }
    bolt.on = 1;
    bolt.t0 = flash_at = plat_ms();
    en_event(E_LIGHTNING, 0, 0);
    thunder_at = plat_ms() + 300u + (uint32_t)lirnd(1200);
    if (target)
        ignite(target);
    else if (C.biome == B_FOREST && in_map((int)tx, (int)ty) && TERR((int)ty * MW + (int)tx) == T_FOREST && hit && proj.par[P_REGION] &&
             lrnd() < 0.5f)
        region_start();                          /* the woods catch */
}
static void quake_start(void)
{
    int x, y, cnt = 0;
    uint16_t cand[160];
    if (quake.on)
        return;
    memset(&quake, 0, sizeof quake);
    quake.on = 1;
    quake.t0 = plat_ms();
    quake.dur = 4500u;
    quake.ex = cam.x + (lrnd() - 0.5f) * 6.0f;
    quake.ey = cam.y + (lrnd() - 0.5f) * 6.0f;
    if (proj.par[P_DISASTER]) {
        for (y = (int)fm_maxf(0.0f, quake.ey - 12.0f); y <= (int)fm_minf((float)(MH - 1), quake.ey + 12.0f); y++)
            for (x = (int)fm_maxf(0.0f, quake.ex - 14.0f); x <= (int)fm_minf((float)(MW - 1), quake.ex + 14.0f) && cnt < 160; x++) {
                int si = c_ref[y * MW + x];
                if (si && c_st[si].type != BT_CIV && c_st[si].x == x && c_st[si].y == y)
                    cand[cnt++] = (uint16_t)si;
            }
        {
            int want = 3 + lirnd(5);
            quake.n = (uint8_t)(cnt < want ? cnt : want);
        }
        for (x = 0; x < quake.n; x++) {
            int j = lirnd(cnt);
            quake.v[x].si = cand[j];
            quake.v[x].born = c_st[cand[j]].born;
            quake.v[x].at = plat_ms() + 500u + (uint32_t)lirnd((int)quake.dur - 900);
            cand[j] = cand[--cnt];
        }
    }
    news(N_QUAKE, 0, 0);
    en_event(E_QUAKE, (int)(quake.dur / 100u), 0);
}
static int start_robbery(void)
{
    int si = 0, b, k, cnt = 0, pick, h, n;
    rect_t r;
    for (k = 0; k < C.nbl[BT_COM]; k++)
        if (c_st[c_ref[c_bl[BT_COM][k]]].L <= 4)
            cnt++;
    if (!cnt || rob.on)
        return 0;
    pick = lirnd(cnt);
    for (k = 0; k < C.nbl[BT_COM]; k++)
        if (c_st[c_ref[c_bl[BT_COM][k]]].L <= 4 && pick-- == 0) {
            si = c_ref[c_bl[BT_COM][k]];
            break;
        }
    if (!si)
        return 0;
    b = road_near(&c_st[si], 1);
    if (b < 0)
        return 0;
    memset(&rob, 0, sizeof rob);
    r.x0 = c_st[si].x - 12.0f;
    r.y0 = c_st[si].y - 12.0f;
    r.x1 = c_st[si].x + 12.0f;
    r.y1 = c_st[si].y + 12.0f;
    h = pick_st(BT_RES, &r);
    n = h ? find_path(road_near(&c_st[h], 1), b, 1, rob.robber.path, PPATH) : 0;
    if (!n) {
        rob.robber.path[0] = rob.robber.path[1] = rob.robber.path[2] = (uint16_t)b;
        n = 3;
    }
    rob.robber.len = (uint8_t)n;
    rob.robber.v = 0.9f;
    rob.robber.side = 1;
    rob.robber.shirt = 7;
    rob.robber.skin = (uint8_t)lirnd(5);
    rob.on = 1;
    rob.si = (uint16_t)si;
    rob.born = c_st[si].born;
    rob.t0 = plat_ms();
    rob.road = b;
    rob.cop = -1;
    return 1;
}
static int start_crash(void)
{
    int k, i = -1;
    uint16_t cand[30];
    int n = 0;
    if (crash.on || C.maxx <= C.minx)
        return 0;
    for (k = 0; k < 600 && n < 30; k++) {
        int x = C.minx + lirnd(C.maxx - C.minx + 1), y = C.miny + lirnd(C.maxy - C.miny + 1), j = y * MW + x;
        if (c_road[j] == 1 && LANE(j) == 0 && c_plan[j] != PL_BELTWAY)
            cand[n++] = (uint16_t)j;
    }
    if (!n)
        return 0;
    i = cand[lirnd(n)];
    memset(&crash, 0, sizeof crash);
    crash.on = 1;
    crash.i = i;
    crash.x = (float)(i % MW) + 0.5f;
    crash.y = (float)(i / MW) + 0.5f;
    crash.t0 = plat_ms();
    crash.col[0] = (uint8_t)lirnd(10);
    crash.col[1] = (uint8_t)lirnd(10);
    news(N_CRASH, 0, 0);
    en_event(E_CRASH, 0, 0);
    crash.amb = (int8_t)dispatch(VK_AMB, CV_HOSPITAL, crash.x, crash.y, i, &crash.quick);
    crash.cop = lrnd() < 0.5f ? (int8_t)dispatch(VK_POLICE, CV_POLICE, crash.x, crash.y, i, 0) : -1;
    return 1;
}
static int start_ufo(void)
{
    rect_t r = view_rect(-1.0f);
    int k, tp = -1;
    float side;
    if (ufo.on)
        return 0;
    for (k = 0; k < NPED && tp < 0; k++) {
        float x, y;
        if (!peds[k].on || peds[k].park || peds[k].c_until > plat_ms())
            continue;
        ped_pos(&peds[k], &x, &y);
        if (in_rect(&r, x, y))
            tp = k;
    }
    memset(&ufo, 0, sizeof ufo);
    if (tp >= 0) {
        ped_pos(&peds[tp], &ufo.tx, &ufo.ty);
        ufo.shirt = peds[tp].shirt;
        ufo.skin = peds[tp].skin;
        peds[tp].on = 0;
    } else {
        int x, y, cnt = 0, pick;
        for (y = (int)fm_maxf(0.0f, r.y0); y <= (int)fm_minf((float)(MH - 1), r.y1); y++)
            for (x = (int)fm_maxf(0.0f, r.x0); x <= (int)fm_minf((float)(MW - 1), r.x1); x++)
                cnt += c_road[y * MW + x] == 1;
        if (!cnt)
            return 0;
        pick = lirnd(cnt);
        for (y = (int)fm_maxf(0.0f, r.y0); y <= (int)fm_minf((float)(MH - 1), r.y1); y++)
            for (x = (int)fm_maxf(0.0f, r.x0); x <= (int)fm_minf((float)(MW - 1), r.x1); x++)
                if (c_road[y * MW + x] == 1 && pick-- == 0) {
                    ufo.tx = (float)x + 0.9f;
                    ufo.ty = (float)y + 0.5f;
                }
        ufo.shirt = (uint8_t)lirnd(10);
        ufo.skin = (uint8_t)lirnd(5);
    }
    side = lrnd() < 0.5f ? -1.0f : 1.0f;
    ufo.on = 1;
    ufo.ux = ufo.tx + side * (float)cam.vw / (float)cam_ts();
    ufo.uy = ufo.ty - (float)cam.vh / (float)cam_ts() * 0.6f;
    ufo.vx = -side * 2.0f;
    ufo.vy = -3.0f;
    ufo.t0 = plat_ms();
    en_event(E_UFO_ON, 0, 0);
    return 1;
}
static uint32_t arrived_at(int c, uint32_t quick)
{
    (void)quick;
    if (c >= 0 && cars[c].on && cars[c].duty)
        return cars[c].arrived;
    return 0;
}
static void send_home(int c)
{
    if (c >= 0 && c != game_car && cars[c].on && cars[c].keep)
        car_home(&cars[c]);
}
static void incidents_update(float dt)
{
    uint32_t now = plat_ms();
    int f;
    for (f = 0; f < NFIRE; f++) {                /* the fires and the brigade */
        uint32_t arr;
        if (!fires[f].on)
            continue;
        if (!st_alive(fires[f].si, fires[f].born)) {
            fires[f].on = 0;
            send_home(fires[f].truck);
            continue;
        }
        if (fires[f].truck < 0 && (int32_t)(now - fires[f].quick) >= 0) {   /* no truck yet: ask again */
            const st_t *s = &c_st[fires[f].si];
            fires[f].quick = now + 1500u;
            fires[f].truck = (int8_t)dispatch(VK_FIRE, CV_FIRE, fires[f].x, fires[f].y, road_near(s, 1), 0);
        }
        arr = arrived_at(fires[f].truck, 0);
        if (!arr && now - fires[f].t0 > 7000u && (int32_t)(now - fires[f].spread) >= 0) {   /* left burning, it spreads */
            const st_t *s = &c_st[fires[f].si];
            int k2, n2 = 0, pick;
            uint16_t nb[16];
            fires[f].spread = now + 5000u + (uint32_t)lirnd(4000);
            for (k2 = -1; k2 <= s->size && n2 < 16; k2++) {      /* the neighbours touching it */
                int e2, p2[4][2] = {{s->x + k2, s->y - 1}, {s->x + k2, s->y + s->size}, {s->x - 1, s->y + k2}, {s->x + s->size, s->y + k2}};
                for (e2 = 0; e2 < 4 && n2 < 16; e2++) {
                    int si2;
                    if (!in_map(p2[e2][0], p2[e2][1]) || !(si2 = c_ref[p2[e2][1] * MW + p2[e2][0]]))
                        continue;
                    if (c_st[si2].type != BT_CIV && !(c_st[si2].flags & (SF_BURN | SF_DEAD)))
                        nb[n2++] = (uint16_t)si2;
                }
            }
            if (n2 && fires[f].spreads < 2 && lrnd() < 0.4f * (1.0f - 0.7f * wet()) && proj.par[P_DISASTER]) {
                pick = lirnd(n2);
                fires[f].spreads++;
                ignite(nb[pick]);
            }
        }
        if (arr && now - arr > 4500u) {
            c_st[fires[f].si].flags &= (uint8_t)~SF_BURN;
            fires[f].on = 0;
            send_home(fires[f].truck);
            news(N_SAVED, 0, 0);
        } else if (!arr && now - fires[f].t0 > 32000u) {   /* too long alight: it burns down */
            fires[f].on = 0;
            send_home(fires[f].truck);
            if (proj.par[P_DISASTER]) {
                news(N_BURNT, c_st[fires[f].si].type, c_st[fires[f].si].L);
                city_demolish(fires[f].si, 1);
            } else {
                c_st[fires[f].si].flags &= (uint8_t)~SF_BURN;
            }
        }
    }
    if (quake.on) {
        int k;
        for (k = 0; k < quake.n; k++)
            if (!quake.v[k].done && now >= quake.v[k].at) {
                quake.v[k].done = 1;
                if (st_alive(quake.v[k].si, quake.v[k].born))
                    city_demolish(quake.v[k].si, 2);
            }
        if (now - quake.t0 > quake.dur)
            quake.on = 0;
    }
    if (rob.on) {
        if (!st_alive(rob.si, rob.born) && rob.phase < 2)
            rob.on = 0;
        else if (rob.phase == 0) {
            ped_t *p = &rob.robber;
            p->d += p->v * dt;
            while (p->d >= 1.0f && p->k < p->len - 1) {
                p->d -= 1.0f;
                p->k++;
            }
            if (p->k >= p->len - 1) {
                rob.phase = 1;
                rob.t1 = now;
                news(N_ROBBERY, c_st[rob.si].type, c_st[rob.si].L);
                rob.cop = (int8_t)dispatch(VK_POLICE, CV_POLICE, (float)c_st[rob.si].x, (float)c_st[rob.si].y, rob.road, &rob.quick);
            }
        } else if (rob.phase == 1) {
            if (rob.cop < 0 && (int32_t)(now - rob.quick) >= 0) {   /* every car out: ask again */
                rob.quick = now + 1500u;
                rob.cop = (int8_t)dispatch(VK_POLICE, CV_POLICE, (float)c_st[rob.si].x, (float)c_st[rob.si].y, rob.road, 0);
            }
            if (arrived_at(rob.cop, rob.quick)) {
                rob.phase = 2;
                rob.t2 = now;
            } else if (now - rob.t1 > 40000u) {
                ped_t *p = &rob.robber;
                int a = 0, b = p->len - 1;
                while (a < b) {
                    uint16_t t = p->path[a];
                    p->path[a++] = p->path[b];
                    p->path[b--] = t;
                }
                p->k = 0;
                p->d = 0.0f;
                p->v = 1.7f;
                rob.phase = 3;
                news(N_ESCAPED, 0, 0);
                send_home(rob.cop);
            }
        } else if (rob.phase == 2) {
            if (now - rob.t2 > 3000u) {
                rob.on = 0;
                news(N_CAUGHT, c_st[rob.si].type, c_st[rob.si].L);
                send_home(rob.cop);
            }
        } else {
            ped_t *p = &rob.robber;
            p->d += p->v * dt;
            while (p->d >= 1.0f && p->k < p->len - 1) {
                p->d -= 1.0f;
                p->k++;
            }
            if (p->k >= p->len - 1)
                rob.on = 0;
        }
    }
    if (crash.on) {
        if (crash.phase == 0) {
            if (crash.amb < 0 && (int32_t)(now - crash.quick) >= 0) {
                crash.quick = now + 1500u;
                crash.amb = (int8_t)dispatch(VK_AMB, CV_HOSPITAL, crash.x, crash.y, crash.i, 0);
            }
            if (arrived_at(crash.amb, crash.quick)) {
                crash.phase = 1;
                crash.t2 = now;
            } else if (now - crash.t0 > 50000u) {
                crash.on = 0;
                news(N_TOWED, 0, 0);
                send_home(crash.amb);
                send_home(crash.cop);
            }
        } else if (now - crash.t2 > 4500u) {
            crash.on = 0;
            news(N_TREATED, 0, 0);
            send_home(crash.amb);
            send_home(crash.cop);
        }
    }
    if (ufo.on) {
        if (ufo.phase == 0) {
            float dx = ufo.tx - ufo.ux, dy = ufo.ty - 0.15f - ufo.uy, d = lhyp(dx, dy), st = fm_clampf(d * 1.6f, 0.6f, 5.0f) * dt;
            if (d <= st) {
                ufo.ux = ufo.tx;
                ufo.uy = ufo.ty - 0.15f;
                ufo.phase = 1;
                ufo.t1 = now;
                en_event(E_BEAM, 0, 0);
                news(N_UFO, 0, 0);
            } else {
                ufo.ux += dx / d * st;
                ufo.uy += dy / d * st;
            }
        } else if (ufo.phase == 1) {
            ufo.lift = fm_minf(1.0f, (float)(now - ufo.t1) / 3500.0f);
            if (now - ufo.t1 > 4300u) {
                ufo.phase = 2;
                ufo.t2 = now;
            }
        } else {
            float k = 1.0f + (float)(now - ufo.t2) / 500.0f;
            ufo.ux += ufo.vx * k * dt;
            ufo.uy += ufo.vy * k * dt;
            if (now - ufo.t2 > 4000u) {
                ufo.on = 0;
                en_event(E_UFO_OFF, 0, 0);
            }
        }
    }
}

#include "region.c"

/* the camera's subject: the oldest incident going on */
static int incident_at(float *x, float *y)
{
    uint32_t t0 = 0xFFFFFFFFu;
    int f, any = 0;
#define CONSIDER(t, X, Y) do { if ((t) < t0) { t0 = (t); *x = (X); *y = (Y); any = 1; } } while (0)
    for (f = 0; f < NFIRE; f++)
        if (fires[f].on)
            CONSIDER(fires[f].t0, fires[f].x, fires[f].y);
    if (rob.on) {
        float rx, ry;
        if (rob.phase == 0 || rob.phase == 3) {
            ped_pos(&rob.robber, &rx, &ry);
            CONSIDER(rob.t0, rx, ry);
        } else {
            CONSIDER(rob.t0, (float)c_st[rob.si].x + 0.5f, (float)c_st[rob.si].y + 0.5f);
        }
    }
    if (crash.on)
        CONSIDER(crash.t0, crash.x, crash.y);
    if (ufo.on)
        CONSIDER(ufo.t0, ufo.phase == 2 ? ufo.tx : ufo.ux, ufo.phase == 2 ? ufo.ty : ufo.uy);
    if (quake.on)
        CONSIDER(quake.t0, quake.ex, quake.ey);
    if (rg.on)
        CONSIDER(rg.t0, rg.x, rg.y);
    if (fest.on)
        CONSIDER(fest.t0, fest.x, fest.y);
    if (blackout.on)
        CONSIDER(blackout.t0, blackout.x, blackout.y);
    if (show.on)                                 /* the show: the camera's first */
        CONSIDER(0u, show.x, show.y - (show.kind == B_DESERT ? fm_minf(6.0f, fm_maxf(0.0f, show_t() - 5.0f) * 0.6f) : 0.0f));
#undef CONSIDER
    return any;
}
static int life_focus(float *x, float *y) { return proj.par[P_FOLLOW] && incident_at(x, y); }
static void shake(int *dx, int *dy)
{
    float k, a;
    *dx = *dy = 0;
    if (rg.on && (rg.kind == B_VOLCANO || rg.kind == B_ALPINE) && plat_ms() - rg.t0 < 2500u) {   /* the blast, the slide */
        a = 2.0f * (1.0f - (float)(plat_ms() - rg.t0) / 2500.0f);
        *dx = (int)((lrnd() - 0.5f) * 2.0f * a);
        *dy = (int)((lrnd() - 0.5f) * 2.0f * a);
        return;
    }
    if (!quake.on)
        return;
    k = fm_maxf(0.0f, 1.0f - (float)(plat_ms() - quake.t0) / (float)quake.dur);
    a = k * 2.5f + 0.4f;
    *dx = (int)((lrnd() - 0.5f) * 2.0f * a);
    *dy = (int)((lrnd() - 0.5f) * 2.0f * a);
}

/* ------------------------------------------------------------- the feed --- */
static uint32_t look_t, sweep_t, last_bar = 0xFFFFFFFFu;
static float mood_view, bright_view, grit_view;
static uint8_t hwy_view;
static void read_view(void)
{
    rect_t r = view_rect(0.0f);
    int x, y, g = 0, ind = 0, n = 0, h = 0;
    float lvs = 0.0f;
    for (y = (int)fm_maxf(0.0f, r.y0); y <= (int)fm_minf((float)(MH - 1), r.y1); y++)
        for (x = (int)fm_maxf(0.0f, r.x0); x <= (int)fm_minf((float)(MW - 1), r.x1); x++) {
            int i = y * MW + x, b = c_bt[i];
            if (!b)
                continue;
            n++;
            lvs += LVF(i);
            if (b == BT_IND)
                ind++;
            else if (b == BT_CIV && civ_green(c_st[c_ref[i]].kind))
                g++;
        }
    for (x = 0; x < NCAR; x++)
        if (cars[x].on) {
            float cx, cy;
            car_pos(&cars[x], &cx, &cy, 0, 0);
            if (c_plan[cars[x].path[cars[x].k]] == PL_BELTWAY && in_rect(&r, cx, cy))
                h++;
        }
    mood_view = ((float)g * 1.2f - (float)ind) / fm_maxf(8.0f, (float)n * 0.25f);
    bright_view = n ? fm_minf(1.0f, lvs / (float)n) : 0.3f;
    grit_view = n ? (float)ind / (float)n : 0.0f;
    hwy_view = (uint8_t)(h > 255 ? 255 : h);
}
static void feed(void)
{
    rect_t vr = view_rect(0.0f);
    int f, b = 0, g = 0, fi = 0, sf = 0, sp = 0, sa = 0;
    float mood;
    if (plat_ms() - look_t >= 500u) {
        look_t = plat_ms();
        read_view();
    }
    mood = mood_view - 0.55f * (1.0f - day_lit()) - 0.35f * wlvl(W_STORM) - 0.15f * wlvl(W_RAIN) + 0.1f * wlvl(W_SNOW) -
           (rg.on ? 0.4f : 0.0f) + (fest.on ? 0.5f : 0.0f) - (blackout.on ? 0.3f : 0.0f) +
           (season.idx == 0 ? 0.15f : season.idx == 1 ? 0.1f : season.idx == 2 ? -0.1f : -0.05f);
    if (proj.par[P_MOOD] == 1)
        mood = 0.8f;
    else if (proj.par[P_MOOD] == 2)
        mood = -0.6f;
    for (f = 0; f < NFLOCK; f++)
        if (flock[f].on && in_rect(&vr, flock[f].x, flock[f].y)) {
            b += flock[f].n;
            g += flock[f].gull;
        }
    for (f = 0; f < NFIRE; f++)
        fi += fires[f].on && in_rect(&vr, fires[f].x, fires[f].y);
    for (f = 0; f < NCAR; f++)
        if (cars[f].on && cars[f].duty && !cars[f].arrived) {
            sf += cars[f].kind == VK_FIRE;
            sp += cars[f].kind == VK_POLICE;
            sa += cars[f].kind == VK_AMB;
        }
    en_city.mood = mood;
    en_city.bright = bright_view;
    en_city.grit = grit_view;
    en_city.rush = rush();
    en_city.day = day_lit();
    /* the sky, as the music hears it: water falling, a storm's dark, fog, snow, the wind */
    en_city.rain = fm_minf(1.0f, llvl(LK_RAIN) + llvl(LK_STORM) * 0.3f + llvl(LK_TYPHOON) * 0.8f + llvl(LK_SLEET) * 0.6f);
    en_city.storm = llvl(LK_STORM) + llvl(LK_TYPHOON) + llvl(LK_BLIZZARD) * 0.6f + llvl(LK_DUST) * 0.4f;
    en_city.fog = llvl(LK_FOG) + llvl(LK_HAZE) * 0.4f + llvl(LK_ASHHAZE) * 0.8f + llvl(LK_ASH) * 0.5f;
    en_city.snow = llvl(LK_SNOW) + llvl(LK_BLIZZARD) + llvl(LK_FROST) * 0.4f + llvl(LK_SLEET) * 0.3f;
    {
        float wind = llvl(LK_DUST) + llvl(LK_BLIZZARD) + llvl(LK_TYPHOON) * 0.7f + llvl(LK_STORM) * 0.2f;
        float rum = 0.0f;
        if (rg.on) {
            float d = lhyp(rg.x - cam.x, rg.y - cam.y), near = fm_clampf(1.0f - d / 30.0f, 0.2f, 1.0f);
            uint32_t el = plat_ms() - rg.t0;
            float live = el < rg.dur - 4000u ? 1.0f : fm_maxf(0.0f, (float)(rg.dur - el) / 4000.0f);
            if (rg_vortex() && !(rg.kind == B_PLAINS && el < RG_WARN))
                wind = fm_maxf(wind, (rg.kind == B_PLAINS ? 1.0f : 0.7f) * near * live);
            else if (!rg_vortex())
                rum = near * live * (rg.kind == B_FOREST ? 0.4f : 1.0f);
        }
        en_city.wind = fm_minf(1.0f, wind);
        en_city.rumble = fm_minf(1.0f, rum);
    }
    en_city.biome = C.biome;
    en_city.alert = (uint8_t)(rg.on && (rg.kind == B_PLAINS || rg.kind == B_BEACH) && plat_ms() - rg.t0 < RG_WARN + 9000u);
    en_city.blackout = blackout.on;
    en_city.festival = fest.on;
    en_city.hwy = hwy_view;
    en_city.birds = (uint8_t)(b > 255 ? 255 : b);
    en_city.gulls = (uint8_t)g;
    en_city.fires = (uint8_t)fi;
    en_city.siren_fire = (uint8_t)sf;
    en_city.siren_police = (uint8_t)sp;
    en_city.siren_amb = (uint8_t)sa;
    en_city.alarm = (uint8_t)(rob.on && rob.phase == 1);
    en_city.ufo = ufo.on;
    en_city.tension = (uint8_t)(fm_maxf(fm_fabsf(C.dem_r), fm_maxf(fm_fabsf(C.dem_c), fm_fabsf(C.dem_i))) > 0.22f);
    en_city.season = season.idx;
    en_city.stage = C.stage;
}

/* ---------------------------------------------------------- the update --- */
static uint32_t life_t;
static float rate_per_ms(int per_hour) { return (float)per_hour / 3600000.0f; }
static void life_reset(void)
{
    int k;
    memset(fires, 0, sizeof fires);
    memset(&rob, 0, sizeof rob);
    memset(&crash, 0, sizeof crash);
    if (ufo.on)
        en_event(E_UFO_OFF, 0, 0);
    memset(&ufo, 0, sizeof ufo);
    memset(&quake, 0, sizeof quake);
    extras_reset();
    memset(flock, 0, sizeof flock);
    memset(boats, 0, sizeof boats);
    memset(dust, 0, sizeof dust);
    bolt.on = 0;
    for (k = 0; k < NCAR; k++)
        cars[k].on = 0;
    for (k = 0; k < NPED; k++)
        peds[k].on = 0;
    ncars = npeds = 0;
    solo_car = -1;
    demo_r = c_demo_w;
    for (k = 0; k < NCLOUD; k++)
        cloud[k].r = 0.0f;
}
static void life_init(void)
{
    lrs ^= plat_ms() * 2654435761u | 1u;
    set_phase(0.29f);
    season_update();
    life_reset();
}
static void life_update(void)
{
    uint32_t now = plat_ms(), dms = now - life_t;
    float dt;
    int k;
    if (dms > 100u)
        dms = 100u;
    life_t = now;
    dt = (float)dms * 0.001f;
    {   /* the clock */
        uint32_t L = day_len();
        if (L) {
            float p0 = phase();
            tm.clock += (float)dms;
            tm.days += (float)dms / (float)L;
            while (tm.clock >= (float)L)
                tm.clock -= (float)L;
            if (p0 < 0.26f && phase() >= 0.26f && music_on())
                en_event(E_JINGLE, 0, 0);
            if (p0 < 0.76f && phase() >= 0.76f && music_on())
                en_event(E_DUSK, 0, 0);
        } else {
            tm.days += (float)dms / 240000.0f;
        }
    }
    season_update();
    wx.blend = fm_minf(1.0f, (float)(now - wx.fade_t) / 6000.0f);
    if (proj.par[P_WEATHER] > 0)
        weather_set(proj.par[P_WEATHER] - 1);
    else if ((int32_t)(now - wx.next) >= 0) {
        float pace = (float)proj.par[P_WPACE] * 0.25f;
        wx.next = now + (uint32_t)((45000.0f + lrnd() * 60000.0f) / fm_maxf(pace, 0.25f));
        weather_set(weather_pick());
    }
    cloud_amt = fm_minf(1.0f, wlvl(W_CLOUDY) + wet() + wlvl(W_SNOW) * 0.8f + wlvl(W_FOG) * 0.3f);
    {
        float vw = (float)cam.vw / (float)cam_ts(), vh = (float)cam.vh / (float)cam_ts();
        for (k = 0; k < NCLOUD; k++) {
            if (cloud[k].r <= 0.0f) {
                cloud[k].x = cam.x - vw + lrnd() * vw * 2.0f;
                cloud[k].y = cam.y - vh + lrnd() * vh * 2.0f;
                cloud[k].r = 3.0f + lrnd() * 5.0f;
            }
            cloud[k].x += 0.8f * dt;
            if (cloud[k].x - cloud[k].r * 2.0f > cam.x + vw * 0.5f + 4.0f || cloud[k].x < cam.x - vw - 20.0f ||
                fm_fabsf(cloud[k].y - cam.y) > vh * 1.5f) {
                cloud[k].x = cam.x - vw * 0.5f - cloud[k].r * 2.0f - lrnd() * 6.0f;
                cloud[k].y = cam.y + (lrnd() - 0.5f) * vh * 1.4f;
            }
        }
    }
    if (proj.par[P_BIRDS] && (int32_t)(now - flock_next) >= 0) {
        flock_next = now + (uint32_t)(((dusk() > 0.3f ? 5000.0f : 11000.0f) + lrnd() * 16000.0f) * 10.0f / (float)proj.par[P_BIRDS]);
        if (day_lit() > 0.12f && wlvl(W_STORM) < 0.5f)
            spawn_flock();
    }
    {
        rect_t r6 = view_rect(6.0f);
        for (k = 0; k < NFLOCK; k++)
            if (flock[k].on) {
                flock[k].x += flock[k].vx * dt;
                flock[k].y += flock[k].vy * dt;
                if (!in_rect(&r6, flock[k].x, flock[k].y) && now - flock[k].t0 > 3000u)
                    flock[k].on = 0;
            }
    }
    {   /* lightning: in a storm most, in rain and snow now and then */
        float sky = lightning_sky();
        if (proj.par[P_LIGHTNING] && sky > 0.3f && (int32_t)(now - strike_next) >= 0) {
            strike_next = now + (uint32_t)((4000.0f + lrnd() * 9000.0f) / sky);
            strike(lrnd() < (float)proj.par[P_LIGHTNING] * 0.05f);
        }
    }
    if (thunder_at && (int32_t)(now - thunder_at) >= 0) {
        thunder_at = 0;
        en_event(E_THUNDER, 0, 0);
    }
    if (proj.par[P_QUAKES] && !quake.on && lrnd() < rate_per_ms(proj.par[P_QUAKES]) * (float)dms)
        quake_start();
    if (proj.par[P_ROBBERY] && !rob.on && lrnd() < rate_per_ms(proj.par[P_ROBBERY]) * (float)dms)
        start_robbery();
    if (proj.par[P_CRASH] && !crash.on && lrnd() < rate_per_ms(proj.par[P_CRASH]) * (float)dms)
        start_crash();
    if (proj.par[P_UFO] && !ufo.on && lrnd() < rate_per_ms(proj.par[P_UFO]) * 0.1f * (float)dms)
        start_ufo();
    {   /* the biome's disaster: more in its weather (a tornado in a storm, a flood in the rain, an avalanche in snow) */
        float k2 = 1.0f;
        switch (C.biome) {
        case B_PLAINS: k2 = 1.0f + 4.0f * wlvl(W_STORM); break;
        case B_RIVER: k2 = 0.3f + 3.0f * (wlvl(W_RAIN) + wlvl(W_STORM)); break;
        case B_ALPINE: k2 = 0.5f + 2.5f * (wlvl(W_SNOW) + wlvl(W_STORM)); break;
        case B_DESERT: k2 = 1.0f + 2.0f * wlvl(W_STORM); break;
        case B_FOREST: k2 = (1.0f - wet()) * (season.idx == 1 || season.idx == 2 ? 1.6f : 0.6f) * (1.0f - wlvl(W_SNOW)); break;
        default: break;
        }
        if (proj.par[P_REGION] && !rg.on && C.stage >= 1 && lrnd() < rate_per_ms(proj.par[P_REGION]) * k2 * (float)dms)
            region_start();
    }
    if (proj.par[P_FEST] && !fest.on && C.stage >= 1 && lrnd() < rate_per_ms(proj.par[P_FEST]) * (float)dms)
        festival_start();
    {   /* a blackout: any time, likelier in extreme weather and in a disaster */
        float k3 = 1.0f + 4.0f * (llvl(LK_STORM) + llvl(LK_TYPHOON) + llvl(LK_BLIZZARD) + llvl(LK_DUST)) + (rg.on ? 3.0f : 0.0f);
        if (proj.par[P_BLACKOUT] && !blackout.on && lrnd() < rate_per_ms(proj.par[P_BLACKOUT]) * k3 * (float)dms)
            blackout_start();
    }
    if (proj.par[P_METEOR] && !meteors.on && day_lit() < 0.2f && wlvl(W_CLEAR) > 0.6f &&
        lrnd() < rate_per_ms(proj.par[P_METEOR]) * 3.0f * (float)dms)
        meteors_start();
    WHERE(41);
    cars_update(dt);
    WHERE(42);
    peds_update(dt, dms);
    WHERE(43);
    boats_update(dt);
    WHERE(44);
    incidents_update(dt);
    WHERE(45);
    region_update(dt);
    WHERE(46);
    extras_update();
    show_update();
    specials_update();
    WHERE(47);
    {   /* what fell: dust, and a crunch if it's on screen */
        rect_t r1 = view_rect(1.0f);
        while (demo_r != c_demo_w) {
            const demo_t *d = &c_demo[demo_r % 16u];
            int n = 7 * d->size * d->size, j;
            demo_r++;
            for (j = 0; j < NDUST && n > 0; j++)
                if (!dust[j].on || now - dust[j].t0 > 1600u) {
                    dust[j].on = 1;
                    dust[j].x = (float)d->x + lrnd() * (float)d->size;
                    dust[j].y = (float)d->y + lrnd() * (float)d->size;
                    dust[j].vx = (lrnd() - 0.5f) * 0.9f;
                    dust[j].vy = (lrnd() - 0.5f) * 0.9f - 0.3f;
                    dust[j].r = 0.15f + lrnd() * 0.3f;
                    dust[j].t0 = now;
                    n--;
                }
            if (in_rect(&r1, (float)d->x + (float)d->size * 0.5f, (float)d->y + (float)d->size * 0.5f))
                en_event(E_CRUMBLE, 0, 0);
        }
    }
    /* rubble goes at a musical phrase's start (four bars), or every seven seconds while the music rests */
    if (music_on()) {
        uint32_t b = en_bars;
        if (b != last_bar && b % 4u == 0u) {
            if (city_sweep())
                en_event(E_SWEEP, 0, 0);
        }
        last_bar = b;
    } else if (now - sweep_t > 7000u) {
        sweep_t = now;
        city_sweep();
    }
    feed();
}
