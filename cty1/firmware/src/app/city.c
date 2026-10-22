/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1: the city's growth (see city.h). Deterministic from the seed: the same seed grows the same town
 * (until fires, quakes and the like change it). Part of the unity build (ui.c includes it); the host tests
 * build it alone. */
#include "city.h"
#include "../dsp/fastmath.h"

city_t C;
uint8_t c_terr[MN] EN_POOL, c_fl2[MN] EN_POOL, c_hgt[MN] EN_POOL, c_road[MN] EN_POOL, c_plan[MN] EN_POOL, c_lm[MN] EN_POOL,
    c_bt[MN] EN_POOL, c_lv[MN] EN_POOL, c_zr[MN] EN_POOL;
uint16_t c_ref[MN] EN_POOL;
st_t c_st[NST] EN_POOL;
uint16_t c_empty[MN] EN_POOL;
uint16_t c_bl[4][NBL] EN_POOL;
uint16_t c_scr[MN] EN_POOL;
uint16_t c_civ[128];
uint16_t c_round[64];
news_t c_news[NNEWS];
uint32_t c_news_w;
demo_t c_demo[16];
uint32_t c_demo_w;

/* ------------------------------------------------------------ tables --- */
const char *const STAGE_NAME[4] = {"Small town", "Suburb", "City", "Huge city"};
const int32_t STAGE_MIN[4] = {0, 1500, 8000, 40000};
static const uint8_t STAGE_MAXLV[4] = {2, 3, 4, 5};
static const float STAGE_UPG[4] = {0.25f, 0.4f, 0.55f, 0.65f};
static const float REQ[5] = {0.0f, 0.2f, 0.38f, 0.54f, 0.68f};
static const int32_t CAP[4][5] = {{0, 0, 0, 0, 0}, {4, 16, 70, 280, 1100}, {6, 24, 90, 380, 1400}, {10, 40, 120, 260, 480}};
static const char *const BNAME[4][5] = {
    {0, 0, 0, 0, 0},
    {"House", "Townhouses", "Apartments", "Mid-rise", "Res. tower"},
    {"Corner store", "Shop row", "Plaza", "Office block", "Glass tower"},
    {"Workshop", "Warehouse", "Factory", "Plant", "Logistics"}};
static const char *const BIGNAME[4][6] = {
    {0}, {0}, {0, 0, 0, "Shopping ctr", "Office campus", "Skyscraper"}, {0, 0, 0, "Factory", "Power plant", "Logistics hub"}};
const char *const CIVIC_NAME[CV_N] = {"Town hall", "Gas station", "Fire station", "Police", "School", "Hospital", "Station",
                                      "Stadium", "Park", "Pocket park", "Playground", "Garden", "Town square", "Pond park",
                                      "Sports fields", "Cemetery", "Marina", "Golf course"};
typedef struct { int16_t jobs; float lv; int8_t r, size; } civic_t;
static const civic_t CIVIC[CV_N] = {
    {20, 0.25f, 5, 2}, {4, 0, 0, 1}, {15, 0, 0, 1}, {25, 0, 0, 1}, {30, 0.1f, 5, 2}, {200, 0.08f, 6, 2}, {60, 0.25f, 8, 2},
    {150, 0.1f, 6, 3}, {0, 0.18f, 4, 2}, {0, 0.1f, 3, 1}, {0, 0.08f, 3, 1}, {0, 0.08f, 3, 1}, {0, 0.15f, 4, 1},
    {0, 0.2f, 5, 2}, {0, 0.06f, 4, 2}, {0, 0, 0, 2}, {0, 0.12f, 4, 2}, {0, 0.1f, 6, 3}};
int civ_green(int kind) { return kind >= CV_GREEN0; }
static const struct { int32_t pop; uint8_t kind; } MILESTONES[] = {
    {0, CV_TOWNHALL}, {40, CV_GAS}, {220, CV_SCHOOL}, {400, CV_POLICE}, {600, CV_FIRE}, {2200, CV_SCHOOL}, {3500, CV_HOSPITAL},
    {7000, CV_GAS}, {9000, CV_FIRE}, {9500, CV_POLICE}, {10000, CV_STATION}, {18000, CV_SCHOOL}, {24000, CV_HOSPITAL},
    {26000, CV_POLICE}, {30000, CV_STATION}, {46000, CV_STADIUM}, {55000, CV_STATION}, {60000, CV_FIRE}, {62000, CV_POLICE},
    {80000, CV_HOSPITAL}};
#define NMILE ((int)(sizeof MILESTONES / sizeof MILESTONES[0]))
/* the green spaces each stage favours (weights, by CV_ - CV_GREEN0: park pocket playground garden plaza pond fields
 * cemetery marina golf) */
static const uint8_t GREENS[4][10] = {{2, 3, 2, 2, 0, 0, 0, 0, 0, 0},
                                      {3, 1, 2, 2, 0, 2, 2, 1, 1, 0},
                                      {3, 2, 1, 0, 2, 2, 2, 1, 1, 1},
                                      {3, 3, 1, 0, 3, 1, 1, 0, 0, 1}};
static const char *const NAME_A[24] = {"Ash", "Bel", "Cedar", "Clear", "Fair", "Glen", "Iron", "Lake", "Maple", "Mill", "North", "Oak",
                                       "Pine", "Red", "River", "Silver", "Stone", "Sun", "West", "Wil", "Elm", "Harbor", "Copper", "Bright"};
static const char *const NAME_B[14] = {"ford", "port", "ton", "field", "brook", "ridge", "view", "ville", "dale", "haven", "wood",
                                       "gate", "crest", "bury"};
/* firsts (C.firsts) */
enum { FI_BRIDGE, FI_CAUSEWAY, FI_HWY, FI_PORT, FI_FARM, FI_FARMGONE, FI_REDEV, FI_SQUARE, FI_GREEN0 = 12 };
#define FIRST(b) (C.firsts >> (b) & 1u)
#define SETFIRST(b) (C.firsts |= 1u << (b))

/* --------------------------------------------------------- randomness --- */
uint32_t city_hash(uint32_t seed, int32_t x, int32_t y)
{
    uint32_t h = seed ^ ((uint32_t)x * 374761393u) ^ ((uint32_t)y * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float city_hashf(uint32_t seed, int32_t x, int32_t y) { return (float)(city_hash(seed, x, y) >> 8) * (1.0f / 16777216.0f); }
static uint32_t rnd_u32(void)                    /* mulberry32 */
{
    uint32_t t;
    C.rs += 0x6D2B79F5u;
    t = C.rs;
    t = (t ^ (t >> 15)) * (1u | t);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return t ^ (t >> 14);
}
static float rnd(void) { return (float)(rnd_u32() >> 8) * (1.0f / 16777216.0f); }
/* the high word of a 32 x 32 multiply, without 64-bit arithmetic (the device has no helpers for it) */
static uint32_t mulhi32(uint32_t a, uint32_t b)
{
    uint32_t al = a & 0xFFFFu, ah = a >> 16, bl = b & 0xFFFFu, bh = b >> 16;
    uint32_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint32_t mid = (ll >> 16) + (lh & 0xFFFFu) + (hl & 0xFFFFu);
    return hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
}
static int irnd(int n) { return n > 0 ? (int)mulhi32(rnd_u32(), (uint32_t)n) : 0; }
static float vnoise(uint32_t seed, float x, float y)
{
    float fx = fm_floorf(x), fy = fm_floorf(y), xf = x - fx, yf = y - fy;
    int32_t xi = (int32_t)fx, yi = (int32_t)fy;
    float u = xf * xf * (3.0f - 2.0f * xf), v = yf * yf * (3.0f - 2.0f * yf);
    float a = city_hashf(seed, xi, yi), b = city_hashf(seed, xi + 1, yi), c = city_hashf(seed, xi, yi + 1), d = city_hashf(seed, xi + 1, yi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
static float fbm(uint32_t seed, float x, float y, int oct)
{
    float s = 0.0f, a = 0.5f, f = 1.0f, n = 0.0f;
    int i;
    for (i = 0; i < oct; i++) {
        s += a * vnoise(seed + (uint32_t)i * 1013u, x * f, y * f);
        n += a;
        a *= 0.5f;
        f *= 2.0f;
    }
    return s / n;
}
static float hyp(float x, float y) { return fm_sqrtf(x * x + y * y); }
static float atan2_t(float y, float x)           /* radians, -pi..pi (a few thousandths) */
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a, s, r;
    if (ax < 1e-9f && ay < 1e-9f)
        return 0.0f;
    a = ax > ay ? ay / ax : ax / ay;
    s = a * a;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax)
        r = 1.57079637f - r;
    if (x < 0.0f)
        r = 3.14159274f - r;
    return y < 0.0f ? -r : r;
}

void city_name(uint32_t seed, char *out)
{
    const char *a = NAME_A[(city_hash(seed, 1, 2) * 24ull) >> 32], *b = NAME_B[(city_hash(seed, 3, 4) * 14ull) >> 32];
    int n = 0, k = 0;
    while (a[n] && n < 10) {
        out[n] = a[n];
        n++;
    }
    if (n && out[n - 1] == b[0])                 /* "Brightton" -> "Brighton" */
        k = 1;
    while (b[k] && n < 15)
        out[n++] = b[k++];
    out[n] = 0;
}

void news(int code, int a, int b)
{
    news_t *e = &c_news[c_news_w % NNEWS];
    e->code = (uint8_t)code;
    e->a = (uint8_t)a;
    e->b = (uint8_t)b;
    c_news_w++;
}

/* ------------------------------------------------------------ helpers --- */
static int32_t cap_of(int type, int L, int size) { return CAP[type][L - 1] * (size > 1 ? 3 : 1); }
static void add_jobs(int type, int32_t v)
{
    if (type == BT_RES)
        C.pop += v;
    else if (type == BT_COM)
        C.jobs_c += v;
    else
        C.jobs_i += v;
}
const char *st_name(const st_t *s)
{
    if (s->type == BT_CIV)
        return CIVIC_NAME[s->kind];
    if (s->flags & SF_FARM)
        return "Farm";
    if (s->size > 1 && s->type >= BT_COM && BIGNAME[s->type][s->L])
        return BIGNAME[s->type][s->L];
    return BNAME[s->type][s->L - 1];
}
static int is_line(int i) { return c_plan[i] != PL_NONE; }
static void grow_bb(int x, int y)
{
    if (x < C.minx) C.minx = (uint8_t)x;
    if (x > C.maxx) C.maxx = (uint8_t)x;
    if (y < C.miny) C.miny = (uint8_t)y;
    if (y > C.maxy) C.maxy = (uint8_t)y;
}
static void clear_ground(int i)
{
    if (TERR(i) == T_FOREST)
        c_terr[i] &= (uint8_t)~3u;
    c_terr[i] &= (uint8_t)~F_TREE;
}
static int st_alloc(void)
{
    static int hint = 1;
    int k;
    for (k = 0; k < NST - 1; k++) {
        int i = 1 + (hint - 1 + k) % (NST - 1);
        if (!c_st[i].type || (c_st[i].flags & SF_DEAD)) {
            int still = 0, j, d, n = c_st[i].size;          /* a dead slot still on the map (rubble keeps none) */
            if (c_st[i].type && (c_st[i].flags & SF_DEAD))
                for (d = 0; d < n * n && !still; d++) {
                    j = (c_st[i].y + d / n) * MW + c_st[i].x + d % n;
                    still = c_ref[j] == i;
                }
            if (still)
                continue;
            hint = i + 1;
            memset(&c_st[i], 0, sizeof c_st[i]);
            return i;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ terrain --- */
static void gen_terrain(void)
{
    const uint32_t seed = C.seed;
    int x, y;
    for (y = 0; y < MH; y++)
        for (x = 0; x < MW; x++)
            c_hgt[y * MW + x] = (uint8_t)fm_clampf(fbm(seed, (float)x / 20.0f, (float)y / 20.0f, 4) * 255.0f, 0.0f, 255.0f);
    C.has_coast = rnd() < 0.6f;
    C.coast_side = (uint8_t)irnd(4);
    if (C.has_coast)
        for (y = 0; y < MH; y++)
            for (x = 0; x < MW; x++) {
                int d = C.coast_side == 0 ? x : C.coast_side == 1 ? MW - 1 - x : C.coast_side == 2 ? y : MH - 1 - y;
                float thr = 6.0f + (fbm(seed + 77u, (float)x / 13.0f, (float)y / 13.0f, 3) - 0.5f) * 16.0f;
                if ((float)d < thr)
                    c_terr[y * MW + x] = T_WATER | F_OCEAN;
            }
    C.has_river = rnd() < 0.85f;
    if (C.has_river) {
        int vertical = C.has_coast ? C.coast_side >= 2 : rnd() < 0.5f, len = vertical ? MH : MW, span = vertical ? MW : MH, a, w;
        float base = (float)span * (0.3f + rnd() * 0.4f), amp = 3.0f + rnd() * 6.0f, f = 0.06f + rnd() * 0.06f, ph = rnd() * 6.28f;
        for (a = 0; a < len; a++) {
            float cf = base + fm_sinf((float)a * f + ph) * amp + (fbm(seed + 500u, (float)a / 9.0f, 0.5f, 2) - 0.5f) * 10.0f;
            int c = (int)fm_floorf(cf + 0.5f);
            for (w = 0; w < 2; w++) {
                int b = c + w;
                if (b < 0 || b >= span)
                    continue;
                x = vertical ? b : a;
                y = vertical ? a : b;
                c_terr[y * MW + x] = (uint8_t)((c_terr[y * MW + x] & F_OCEAN) | T_WATER);
            }
        }
    }
    for (y = 0; y < MH; y++)                     /* woods, groves and scattered trees */
        for (x = 0; x < MW; x++) {
            int i = y * MW + x;
            float wood;
            if (TERR(i) == T_WATER)
                continue;
            wood = fbm(seed + 300u, (float)x / 9.0f, (float)y / 9.0f, 3);
            if (wood > 0.57f)
                c_terr[i] = T_FOREST;
            else if (city_hashf(seed + 11u, x, y) < 0.05f + fm_maxf(0.0f, wood - 0.42f) * 1.1f)
                c_terr[i] |= F_TREE;
        }
    for (y = 0; y < MH; y++)
        for (x = 0; x < MW; x++) {
            int i = y * MW + x, dx, dy;
            if (TERR(i) != T_WATER)
                continue;
            for (dy = -2; dy <= 2; dy++)
                for (dx = -2; dx <= 2; dx++) {
                    int xx = x + dx, yy = y + dy, j;
                    if (!in_map(xx, yy))
                        continue;
                    j = yy * MW + xx;
                    c_terr[j] |= F_NWATER;
                    if (c_terr[i] & F_OCEAN)
                        c_terr[j] |= F_NOCEAN;
                    if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) == 1 && TERR(j) != T_WATER && city_hashf(seed + 21u, xx, yy) < 0.35f)
                        c_terr[j] |= F_TREE;
                }
        }
}

static void pick_center(void)
{
    int x, y, bx = -1, by = -1;
    float bs = -1e9f;
    for (y = 12; y < MH - 12; y++)
        for (x = 14; x < MW - 14; x++) {
            int i = y * MW + x, dx, dy, clear = 1, near = 0;
            float s;
            if (TERR(i) == T_WATER)
                continue;
            for (dy = -3; dy <= 3 && clear; dy++)
                for (dx = -3; dx <= 3; dx++)
                    if (TERR((y + dy) * MW + x + dx) == T_WATER) {
                        clear = 0;
                        break;
                    }
            if (!clear)
                continue;
            s = -hyp((float)x - MW / 2.0f, (float)y - MH / 2.0f) * 0.4f + city_hashf(C.seed, x, y) * 2.0f;
            for (dy = -9; dy <= 9 && !near; dy += 3)
                for (dx = -9; dx <= 9; dx += 3)
                    if (in_map(x + dx, y + dy) && TERR((y + dy) * MW + x + dx) == T_WATER) {
                        near = 1;
                        break;
                    }
            if (near)
                s += 4.0f;
            if (TERR(i) == T_FOREST)
                s -= 3.0f;
            if (s > bs) {
                bs = s;
                bx = x;
                by = y;
            }
        }
    if (bx < 0) {
        int dx, dy;
        bx = MW / 2;
        by = MH / 2;
        for (dy = -4; dy <= 4; dy++)
            for (dx = -4; dx <= 4; dx++)
                c_terr[(by + dy) * MW + bx + dx] = T_LAND;
    }
    C.cx = (uint8_t)bx;
    C.cy = (uint8_t)by;
}

/* ---------------------------------------------------------- road plan --- */
/* the lines: positions along one axis, blocks wider outward, every third line near the middle an avenue */
static int line_positions(int c, int max, int16_t *pos, uint8_t *kind)
{
    int n = 0, dir;
    pos[n] = (int16_t)c;
    kind[n++] = 2;
    for (dir = 1; dir >= -1; dir -= 2) {
        int p = c, k = 0, last_av = 1;
        for (;;) {
            int d = p > c ? p - c : c - p, av = (k + 1) % 3 == 0 && d < 30, gap;
            gap = d < 10 ? 4 + (rnd() < 0.5f) : d < 22 ? 5 + (rnd() < 0.5f) : 6 + irnd(2);
            if ((dir > 0 && last_av) || (dir < 0 && av))
                gap += 1;
            p += dir * gap;
            k++;
            if (p < 1 || p > max - 3 || n >= 31)
                break;
            pos[n] = (int16_t)p;
            kind[n++] = (uint8_t)(av ? 2 : 1);
            last_av = av;
        }
    }
    {   /* sorted (with their kinds) */
        int i, j;
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && pos[j - 1] > pos[j]; j--) {
                int16_t tp = pos[j];
                uint8_t tk = kind[j];
                pos[j] = pos[j - 1];
                kind[j] = kind[j - 1];
                pos[j - 1] = tp;
                kind[j - 1] = tk;
            }
    }
    return n;
}

/* the plan's scratch: what runs north-south (V) and east-west (H), their medians, the cul-de-sacs' ends. Lent from
 * the open-lot list and the routes' scratch, which aren't in use while the city is founded. */
static uint8_t *const PV = (uint8_t *)c_empty, *const PH = (uint8_t *)c_empty + MN;
static uint8_t *const PMV = (uint8_t *)c_scr, *const PMH = (uint8_t *)c_scr + MN;
static uint8_t *const p_end = c_lv;              /* land value isn't worked out yet either */
static int pok(int x, int y) { return x >= 1 && y >= 1 && x < MW - 1 && y < MH - 1; }
static void mark_v(int x, int y, int k)
{
    int i;
    if (!pok(x, y))
        return;
    i = y * MW + x;
    if (PV[i] < k)
        PV[i] = (uint8_t)k;
    if (k >= 2 && pok(x + 1, y)) {
        if (PV[i + 1] < k)
            PV[i + 1] = (uint8_t)k;
        PMV[i] |= 2u;
        PMV[i + 1] |= 1u;
    }
}
static void mark_h(int x, int y, int k)
{
    int i;
    if (!pok(x, y))
        return;
    i = y * MW + x;
    if (PH[i] < k)
        PH[i] = (uint8_t)k;
    if (k >= 2 && pok(x, y + 1)) {
        if (PH[i + MW] < k)
            PH[i + MW] = (uint8_t)k;
        PMH[i] |= 8u;
        PMH[i + MW] |= 4u;
    }
}
static float drop_p(float x, float y) { return fm_clampf((hyp(x - (float)C.cx, y - (float)C.cy) - 10.0f) / 30.0f, 0.0f, 0.4f); }

static void gen_plan(void)
{
    int16_t xs[32], ys[32];
    uint8_t xk[32], yk[32];
    int nx, ny, a, b, i, j, k, x, y;
    memset(PV, 0, MN);
    memset(PH, 0, MN);
    memset(PMV, 0, MN);
    memset(PMH, 0, MN);
    memset(p_end, 0, MN);
    nx = line_positions(C.cx, MW, xs, xk);
    ny = line_positions(C.cy, MH, ys, yk);
    for (a = 0; a < nx; a++)
        for (j = 0; j <= ny; j++) {
            int ya = j == 0 ? 1 : ys[j - 1], yb = j == ny ? MH - 2 : ys[j];
            if (xk[a] == 1 && rnd() < drop_p((float)xs[a], (float)(ya + yb) * 0.5f))
                continue;
            for (y = ya; y <= yb; y++)
                mark_v(xs[a], y, xk[a]);
        }
    for (b = 0; b < ny; b++)
        for (j = 0; j <= nx; j++) {
            int xa = j == 0 ? 1 : xs[j - 1], xb = j == nx ? MW - 2 : xs[j];
            if (yk[b] == 1 && rnd() < drop_p((float)(xa + xb) * 0.5f, (float)ys[b]))
                continue;
            for (x = xa; x <= xb; x++)
                mark_h(x, ys[b], yk[b]);
        }
    {   /* the beltway */
        int rr = 22 + irnd(8), ry = (int)fm_floorf((float)rr * 0.8f + 0.5f);
        int xl = C.cx - rr, xr = C.cx + rr, yt = C.cy - ry, yb2 = C.cy + ry;
        for (x = xl; x <= xr + 1; x++) {
            mark_h(x, yt, 3);
            mark_h(x, yb2, 3);
        }
        for (y = yt; y <= yb2 + 1; y++) {
            mark_v(xl, y, 3);
            mark_v(xr, y, 3);
        }
    }
    for (a = 0; a < nx - 1; a++)                 /* cul-de-sacs into the outer blocks */
        for (b = 0; b < ny - 1; b++) {
            int ix0 = xs[a] + (xk[a] == 2 ? 2 : 1), ix1 = xs[a + 1] - 1, iy0 = ys[b] + (yk[b] == 2 ? 2 : 1), iy1 = ys[b + 1] - 1;
            int w = ix1 - ix0 + 1, h = iy1 - iy0 + 1, okb, okr;
            float d;
            if (w < 4 || h < 4)
                continue;
            d = hyp((float)(ix0 + ix1) * 0.5f - (float)C.cx, (float)(iy0 + iy1) * 0.5f - (float)C.cy);
            if (d < 9.0f || rnd() < (w >= 5 && h >= 5 ? 0.2f : 0.5f))
                continue;
            okb = pok(ix0, iy1 + 1) && PH[(iy1 + 1) * MW + ix0 + (w >> 1)] > 0 && PH[(iy1 + 1) * MW + ix0 + (w >> 1)] < 3;
            okr = pok(ix1 + 1, iy0) && PV[(iy0 + (h >> 1)) * MW + ix1 + 1] > 0 && PV[(iy0 + (h >> 1)) * MW + ix1 + 1] < 3;
            if (!okb && !okr)
                continue;
            if (okb && (!okr || rnd() < 0.5f)) {
                int sx = ix0 + (w >> 1), len = (h + 1) >> 1;
                if (len < 2)
                    len = 2;
                for (k = 0; k < len; k++) {
                    mark_v(sx, iy1 - k, 1);
                    if (pok(sx, iy1 - k))
                        c_fl2[(iy1 - k) * MW + sx] |= F2_STUB;
                }
                if (pok(sx, iy1 - len + 1))
                    p_end[(iy1 - len + 1) * MW + sx] = 1;
            } else {
                int sy = iy0 + (h >> 1), len = (w + 1) >> 1;
                if (len < 2)
                    len = 2;
                for (k = 0; k < len; k++) {
                    mark_h(ix1 - k, sy, 1);
                    if (pok(ix1 - k, sy))
                        c_fl2[sy * MW + ix1 - k] |= F2_STUB;
                }
                if (pok(ix1 - len + 1, sy))
                    p_end[sy * MW + ix1 - len + 1] = 1;
            }
        }
    for (i = 0; i < MN; i++) {
        int v = PV[i], hh = PH[i];
        if (!v && !hh)
            continue;
        c_plan[i] = (uint8_t)(p_end[i] && !(v && hh) ? PL_CULDESAC : v > hh ? v : hh);
        if (v && hh)
            c_lm[i] = 0;
        else if (v)
            c_lm[i] = (uint8_t)(1u | (v >= 2 ? PMV[i] : 0u) << 2);
        else
            c_lm[i] = (uint8_t)(2u | (hh >= 2 ? PMH[i] : 0u) << 2);
    }
    for (i = 0; i < MN; i++)                     /* near an avenue, near the beltway */
        if (c_plan[i] == PL_AVENUE || c_plan[i] == PL_BELTWAY) {
            int r = c_plan[i] == PL_BELTWAY ? 2 : 1, dx, dy;
            uint8_t f = c_plan[i] == PL_BELTWAY ? F2_NHWY : F2_NAV;
            x = i % MW;
            y = i / MW;
            for (dy = -r; dy <= r; dy++)
                for (dx = -r; dx <= r; dx++)
                    if (in_map(x + dx, y + dy))
                        c_fl2[(y + dy) * MW + x + dx] |= f;
        }
    C.ind_angle = C.has_coast ? (C.coast_side == 0 ? 3.14159274f : C.coast_side == 1 ? 0.0f : C.coast_side == 2 ? -1.57079637f : 1.57079637f)
                              : rnd() * 6.28318548f - 3.14159274f;
    C.nrounds = 0;
    for (a = 0; a < nx; a++)                     /* roundabouts where avenues cross */
        if (xk[a] == 2)
            for (b = 0; b < ny; b++)
                if (yk[b] == 2 && rnd() < 0.45f) {
                    int good = 1, d;
                    x = xs[a];
                    y = ys[b];
                    for (d = 0; d < 4; d++)
                        if (!pok(x + (d & 1), y + (d >> 1)) || TERR((y + (d >> 1)) * MW + x + (d & 1)) == T_WATER)
                            good = 0;
                    if (good && C.nrounds < 64) {
                        c_round[C.nrounds++] = (uint16_t)(y * MW + x);
                        for (d = 0; d < 4; d++)
                            c_fl2[(y + (d >> 1)) * MW + x + (d & 1)] |= F2_ROUND;
                    }
                }
    memset(PV, 0, MN);                           /* the borrowed lists back */
    memset(PH, 0, MN);
    memset(PMV, 0, MN);
    memset(PMH, 0, MN);
    memset(p_end, 0, MN);
}

/* -------------------------------------------------------------- roads --- */
static void add_road(int i, int bridge)
{
    int x = i % MW, y = i / MW, dx, dy;
    c_road[i] = (uint8_t)(bridge ? 2 : 1);
    clear_ground(i);
    C.roads++;
    grow_bb(x, y);
    for (dy = -2; dy <= 2; dy++)
        for (dx = -2; dx <= 2; dx++)
            if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= 2 && in_map(x + dx, y + dy))
                c_terr[(y + dy) * MW + x + dx] |= F_NROAD;
}

static void scan(void)
{
    int x, y, x0, x1, y0, y1, t;
    C.nempty = 0;
    for (t = 0; t < 4; t++)
        C.nbl[t] = 0;
    x0 = C.minx - 3 < 1 ? 1 : C.minx - 3;
    x1 = C.maxx + 3 > MW - 2 ? MW - 2 : C.maxx + 3;
    y0 = C.miny - 3 < 1 ? 1 : C.miny - 3;
    y1 = C.maxy + 3 > MH - 2 ? MH - 2 : C.maxy + 3;
    for (y = 0; y < MH; y++)                     /* the open-lot marks, cleared everywhere they could be */
        for (x = 0; x < MW; x++)
            c_terr[y * MW + x] &= (uint8_t)~F_EMPTY;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int i = y * MW + x, bt = c_bt[i];
            if (bt) {
                const st_t *s = &c_st[c_ref[i]];
                if (bt <= BT_IND && s->x == x && s->y == y && C.nbl[bt] < NBL)
                    c_bl[bt][C.nbl[bt]++] = (uint16_t)i;
                continue;
            }
            if (c_road[i] || TERR(i) == T_WATER || !(c_terr[i] & F_NROAD) || RUBBLE(i) || is_line(i))
                continue;
            c_empty[C.nempty++] = (uint16_t)i;
            c_terr[i] |= F_EMPTY;
        }
}

typedef struct { int r, c, i, p, road; } nb_t;
static nb_t neigh(int x, int y, int r)
{
    nb_t n = {0, 0, 0, 0, 0};
    int dx, dy;
    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++) {
            int xx = x + dx, yy = y + dy, j, b;
            if (!in_map(xx, yy))
                continue;
            j = yy * MW + xx;
            if (c_road[j])
                n.road++;
            b = c_bt[j];
            if (b == BT_RES)
                n.r++;
            else if (b == BT_COM)
                n.c++;
            else if (b == BT_IND)
                n.i++;
            else if (b == BT_CIV && civ_green(c_st[c_ref[j]].kind))
                n.p++;
        }
    return n;
}

static int city_radius(void)
{
    int r = 6, v;
    v = C.maxx - C.cx; if (v > r) r = v;
    v = C.cx - C.minx; if (v > r) r = v;
    v = C.maxy - C.cy; if (v > r) r = v;
    v = C.cy - C.miny; if (v > r) r = v;
    return r;
}

/* land value, in hundredths (0..255): near the middle, by water and on high ground; more round the landmarks and
 * parks, less round industry. Worked out in place, each part rounded. */
static void compute_lv(void)
{
    float rad = (float)city_radius();
    int x, y, x0, x1, y0, y1, k;
    x0 = C.minx - 4 < 0 ? 0 : C.minx - 4;
    x1 = C.maxx + 4 > MW - 1 ? MW - 1 : C.maxx + 4;
    y0 = C.miny - 4 < 0 ? 0 : C.miny - 4;
    y1 = C.maxy + 4 > MH - 1 ? MH - 1 : C.maxy + 4;
    memset(c_lv, 0, MN);
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int i = y * MW + x;
            float v = fm_maxf(0.0f, 1.0f - hyp((float)(x - C.cx), (float)(y - C.cy)) / (rad * 1.1f + 3.0f));
            if (c_terr[i] & F_NWATER)
                v += 0.05f;
            if (c_hgt[i] > 168)
                v += 0.03f;
            c_lv[i] = (uint8_t)(v * 100.0f + 0.5f);
        }
    for (k = 0; k < C.nciv; k++) {
        const st_t *c = &c_st[c_civ[k]];
        const civic_t *d = &CIVIC[c->kind];
        float ccx = (float)c->x + (float)(c->size - 1) * 0.5f, ccy = (float)c->y + (float)(c->size - 1) * 0.5f;
        int rx = (int)fm_floorf(ccx + 0.5f), ry = (int)fm_floorf(ccy + 0.5f), dx, dy;
        if (d->lv <= 0.0f)
            continue;
        for (dy = -d->r - 1; dy <= d->r + 1; dy++)
            for (dx = -d->r - 1; dx <= d->r + 1; dx++) {
                int xx = rx + dx, yy = ry + dy, j, v;
                float dd;
                if (xx < x0 || yy < y0 || xx > x1 || yy > y1)
                    continue;
                dd = hyp((float)xx - ccx, (float)yy - ccy);
                if (dd > (float)d->r)
                    continue;
                j = yy * MW + xx;
                v = c_lv[j] + (int)(d->lv * (1.0f - dd / (float)(d->r + 1)) * 100.0f + 0.5f);
                c_lv[j] = (uint8_t)(v > 255 ? 255 : v);
            }
    }
    for (k = 0; k < C.nbl[BT_IND]; k++) {
        const st_t *s = &c_st[c_ref[c_bl[BT_IND][k]]];
        float kk = s->size > 1 ? 1.6f : 1.0f, sx = (float)s->x + (float)(s->size - 1) * 0.5f, sy = (float)s->y + (float)(s->size - 1) * 0.5f;
        int rx = (int)fm_floorf(sx + 0.5f), ry = (int)fm_floorf(sy + 0.5f), dx, dy;
        for (dy = -4; dy <= 4; dy++)
            for (dx = -4; dx <= 4; dx++) {
                int xx = rx + dx, yy = ry + dy, j, v;
                float dd;
                if (xx < x0 || yy < y0 || xx > x1 || yy > y1)
                    continue;
                dd = hyp((float)xx - sx, (float)yy - sy);
                if (dd > 3.0f * kk)
                    continue;
                j = yy * MW + xx;
                v = c_lv[j] - (int)(3.0f * (float)s->L * kk * (1.0f - dd / (4.0f * kk)) + 0.5f);
                c_lv[j] = (uint8_t)(v < 0 ? 0 : v);
            }
    }
}

static int extend_roads(int n)
{
    int any = 0, k;
    static uint16_t bridge[16];
    for (k = 0; k < n; k++) {
        int best = -1, nb = 0, x, y, x0, x1, y0, y1;
        float bs = -1e9f;
        x0 = C.minx - 2 < 1 ? 1 : C.minx - 2;
        x1 = C.maxx + 2 > MW - 2 ? MW - 2 : C.maxx + 2;
        y0 = C.miny - 2 < 1 ? 1 : C.miny - 2;
        y1 = C.maxy + 2 > MH - 2 ? MH - 2 : C.maxy + 2;
        for (y = y0; y <= y1; y++)
            for (x = x0; x <= x1; x++) {
                int i = y * MW + x, dx = 0, dy = 0, d, found = 0, np = 0;
                static const int8_t D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                uint16_t path[16];
                float s;
                if (c_road[i] || c_bt[i] || !is_line(i))
                    continue;
                if (c_plan[i] == PL_BELTWAY && C.stage < 2)
                    continue;
                for (d = 0; d < 4; d++) {
                    int nx = x - D[d][0], ny = y - D[d][1];
                    if (in_map(nx, ny) && c_road[ny * MW + nx]) {
                        dx = D[d][0];
                        dy = D[d][1];
                        found = 1;
                        break;
                    }
                }
                if (!found)
                    continue;
                s = -hyp((float)(x - C.cx), (float)(y - C.cy) * 1.1f) + fbm(C.seed + 900u, (float)x / 12.0f, (float)y / 12.0f, 2) * 14.0f + rnd() * 4.0f;
                if (c_hgt[i] > 173)
                    s -= 3.0f;
                if (TERR(i) == T_FOREST)
                    s -= 2.0f;
                if (c_plan[i] == PL_AVENUE)
                    s += 2.0f;
                else if (c_plan[i] == PL_BELTWAY)
                    s += 1.0f;
                if (c_fl2[i] & F2_STUB)
                    s += 6.0f;
                if (TERR(i) == T_WATER) {
                    int xx = x, yy = y, st, mx = (c_terr[i] & F_OCEAN) ? 11 : 8, ok = 0;
                    if (C.stage < 1 || ((c_terr[i] & F_OCEAN) && C.stage < 2))
                        continue;
                    for (st = 0; st < mx; st++) {
                        int ii;
                        if (xx < 1 || yy < 1 || xx >= MW - 1 || yy >= MH - 1)
                            break;
                        ii = yy * MW + xx;
                        if (TERR(ii) != T_WATER) {
                            ok = 1;
                            break;
                        }
                        path[np++] = (uint16_t)ii;
                        xx += dx;
                        yy += dy;
                    }
                    if (!ok)
                        continue;
                    s -= 5.0f + (float)np * 0.4f;
                }
                if (s > bs) {
                    bs = s;
                    best = i;
                    nb = np;
                    memcpy(bridge, path, sizeof(uint16_t) * (uint32_t)np);
                }
            }
        if (best < 0)
            break;
        any = 1;
        if (nb) {
            int j, sea = 0;
            for (j = 0; j < nb; j++) {
                add_road(bridge[j], 1);
                sea |= (c_terr[bridge[j]] & F_OCEAN) != 0;
            }
            if (!FIRST(FI_BRIDGE)) {
                SETFIRST(FI_BRIDGE);
                news(N_BRIDGE, 0, 0);
            }
            if (sea && !FIRST(FI_CAUSEWAY)) {
                SETFIRST(FI_CAUSEWAY);
                news(N_CAUSEWAY, 0, 0);
            }
        } else {
            add_road(best, 0);
            if (c_plan[best] == PL_BELTWAY && !FIRST(FI_HWY)) {
                SETFIRST(FI_HWY);
                news(N_BELTWAY, 0, 0);
            }
        }
    }
    return any;
}

/* ---------------------------------------------------------- buildings --- */
static void note_first(const st_t *s)
{
    int b = (s->type - 1) * 5 + s->L - 1;
    if (!(C.firsts2 >> b & 1u)) {
        C.firsts2 |= 1u << b;
        if (!(s->type == BT_RES && s->L == 1))
            news(N_FIRST, s->type, s->L);
    }
    if (s->size > 1 && s->type >= BT_COM && s->L >= 3) {
        int bb = 15 + (s->type - BT_COM) * 3 + s->L - 3;
        if (!(C.firsts2 >> bb & 1u)) {
            C.firsts2 |= 1u << bb;
            news(N_FIRSTBIG, s->type, s->L);
        }
    }
    if (s->type == BT_IND && s->L >= 3 && !(s->flags & SF_FARM) && (c_terr[s->y * MW + s->x] & F_NOCEAN) && !FIRST(FI_PORT)) {
        SETFIRST(FI_PORT);
        news(N_PORT, 0, 0);
    }
}
static void set_foot(int si)
{
    st_t *s = &c_st[si];
    int dx, dy;
    for (dy = 0; dy < s->size; dy++)
        for (dx = 0; dx < s->size; dx++) {
            int j = (s->y + dy) * MW + s->x + dx;
            c_ref[j] = (uint16_t)si;
            c_bt[j] = s->type;
            c_terr[j] &= (uint8_t)~F_EMPTY;
            clear_ground(j);
        }
}
static int place(int i, int type, int L)
{
    int si = st_alloc();
    st_t *s;
    if (!si)
        return 0;
    s = &c_st[si];
    s->type = (uint8_t)type;
    s->L = (uint8_t)L;
    s->x = (uint8_t)(i % MW);
    s->y = (uint8_t)(i / MW);
    s->size = 1;
    s->born = C.tick;
    set_foot(si);
    C.buildings++;
    C.dev++;
    add_jobs(type, cap_of(type, L, 1));
    if ((type == BT_RES || type == BT_COM) && L >= 4)
        C.towers++;
    note_first(s);
    news(N_BUILD, type, L);
    return 1;
}
static int max_lv_now(void) { return C.stage == 0 && C.pop < 300 ? 1 : STAGE_MAXLV[C.stage]; }

static int free_n(int x, int y, int n)
{
    int dx, dy;
    if (x + n - 1 >= MW - 1 || y + n - 1 >= MH - 1)
        return 0;
    for (dy = 0; dy < n; dy++)
        for (dx = 0; dx < n; dx++)
            if (!(c_terr[(y + dy) * MW + x + dx] & F_EMPTY))
                return 0;
    return 1;
}

static const float ZFIT[4][6] = {{0}, {0, 0.5f, 0.05f, -0.6f, 0, -0.35f}, {0, -0.1f, 0.6f, -0.4f, 0, -0.5f}, {0, -0.6f, -0.5f, 0.7f, 0, -0.15f}};

static void place_farm(int i)
{
    int x = i % MW, y = i / MW, size = free_n(x, y, 2) ? 2 : 1, si = st_alloc();
    int32_t cap;
    st_t *s;
    if (!si)
        return;
    s = &c_st[si];
    s->type = BT_IND;
    s->L = 1;
    s->x = (uint8_t)x;
    s->y = (uint8_t)y;
    s->size = (uint8_t)size;
    s->born = C.tick;
    s->flags = SF_FARM;
    set_foot(si);
    cap = cap_of(BT_IND, 1, size);
    C.buildings++;
    C.dev++;
    add_jobs(BT_IND, cap);
    C.farm_jobs += cap;
    news(N_BUILD, BT_IND, 0);
    if (!FIRST(FI_FARM)) {
        SETFIRST(FI_FARM);
        news(N_FARMS, 0, 0);
    }
}

static int build(int type, int farm)
{
    int best = -1, k, samples, maxlv = max_lv_now(), L;
    float bs = -1e9f, v;
    if (!C.nempty)
        return 0;
    samples = C.nempty < 70 ? C.nempty : 70;
    for (k = 0; k < samples; k++) {
        int i = c_empty[irnd(C.nempty)], x, y, z;
        float lv, s;
        nb_t nb;
        if (c_bt[i])
            continue;
        x = i % MW;
        y = i / MW;
        lv = LVF(i);
        nb = neigh(x, y, 2);
        z = ZONE(i);
        if (farm)
            s = (z == Z_FARM ? 0.8f : -0.8f) + (1.0f - lv) * 0.5f + (free_n(x, y, 2) ? 0.25f : 0.0f) + rnd() * 0.3f;
        else if (type == BT_RES)
            s = lv - (float)nb.i * 0.12f + (float)nb.p * 0.05f + (c_hgt[i] > 168 ? 0.08f : 0.0f) + rnd() * 0.3f + ZFIT[BT_RES][z];
        else if (type == BT_COM)
            s = lv * 1.5f + (float)nb.c * 0.04f + (float)nb.road * 0.03f - (float)nb.i * 0.05f + rnd() * 0.25f + ZFIT[BT_COM][z];
        else
            s = (1.0f - lv) * 0.9f + (float)nb.i * 0.08f + ((c_terr[i] & F_NWATER) ? 0.25f : 0.0f) - (float)nb.r * 0.05f + rnd() * 0.3f + ZFIT[BT_IND][z];
        if (s > bs) {
            bs = s;
            best = i;
        }
    }
    if (best < 0)
        return 0;
    if (farm) {
        if (ZONE(best) != Z_FARM)
            return 0;
        place_farm(best);
        return 1;
    }
    v = LVF(best);
    L = 1;
    if (type == BT_IND) {
        L = maxlv - 1 < C.stage ? maxlv - 1 : C.stage;
        if (L < 1)
            L = 1;
    } else {
        while (L < maxlv && v >= REQ[L])
            L++;
        if (L > maxlv - 1)
            L = maxlv - 1;
        if (L < 1)
            L = 1;
        if (L > 1 && rnd() < 0.5f)
            L--;
    }
    if (L > 2)
        L = 2;
    return place(best, type, L);
}

static void compute_zones(void)
{
    float rad = (float)city_radius(), core = rad * (0.16f + 0.05f * (float)C.stage) + 2.0f;
    int x, y, x0, x1, y0, y1;
    x0 = C.minx - 8 < 0 ? 0 : C.minx - 8;
    x1 = C.maxx + 8 > MW - 1 ? MW - 1 : C.maxx + 8;
    y0 = C.miny - 8 < 0 ? 0 : C.miny - 8;
    y1 = C.maxy + 8 > MH - 1 ? MH - 1 : C.maxy + 8;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int i = y * MW + x, z = Z_RES;
            float d, n, da;
            if (TERR(i) == T_WATER || c_plan[i]) {
                c_zr[i] &= 0xF0u;
                continue;
            }
            d = hyp((float)(x - C.cx), (float)(y - C.cy));
            n = fbm(C.seed + 1200u, (float)x / 10.0f, (float)y / 10.0f, 2);
            da = fm_fabsf(atan2_t((float)(y - C.cy), (float)(x - C.cx)) - C.ind_angle);
            if (da > 3.14159274f)
                da = 6.28318548f - da;
            if (d < core || ((c_fl2[i] & F2_NAV) && d < rad * 0.75f && n > 0.47f))
                z = Z_COM;
            else if ((da < 0.5f + (n - 0.5f) * 1.2f && d > rad * 0.36f) || ((c_terr[i] & F_NOCEAN) && d > rad * 0.3f && n > 0.45f) ||
                     ((c_fl2[i] & F2_NHWY) && n > 0.55f))
                z = Z_IND;
            if (z == Z_RES && d > rad * 0.8f && C.stage <= 1 && n < 0.56f)
                z = Z_FARM;
            if (z == Z_IND && C.stage == 0 && d > rad * 0.55f)
                z = Z_FARM;
            c_zr[i] = (uint8_t)((c_zr[i] & 0xF0u) | (uint8_t)z);
        }
}

static int misfit(const st_t *s)
{
    int z = ZONE(s->y * MW + s->x);
    if (city_hashf(C.seed + 77u, s->x, s->y) < 0.22f)       /* a few holdouts keep the mix interesting */
        return 0;
    if (s->flags & SF_FARM)
        return z != Z_FARM;
    if (s->type == BT_RES)
        return z == Z_IND || (z == Z_COM && s->L <= 2);
    if (s->type == BT_COM)
        return z == Z_IND || ((z == Z_RES || z == Z_FARM) && s->L >= 3 && s->size > 1);
    if (s->type == BT_IND)
        return z == Z_RES || z == Z_COM;
    return 0;
}

void city_demolish(int si, int cause)
{
    st_t *s = &c_st[si];
    int dx, dy;
    if (!s->type || (s->flags & SF_DEAD))
        return;
    s->flags = (uint8_t)((s->flags | SF_DEAD) & ~SF_BURN);
    if (s->type == BT_CIV) {
        int k, m = 0;
        C.jobs_civ -= CIVIC[s->kind].jobs;
        if (civ_green(s->kind))
            C.greens--;
        for (k = 0; k < C.nciv; k++)
            if (c_civ[k] != si)
                c_civ[m++] = c_civ[k];
        C.nciv = (uint16_t)m;
    } else {
        add_jobs(s->type, -cap_of(s->type, s->L, s->size));
        if (s->flags & SF_FARM)
            C.farm_jobs -= cap_of(BT_IND, 1, s->size);
        if ((s->type == BT_RES || s->type == BT_COM) && s->L >= 4)
            C.towers--;
    }
    C.buildings--;
    for (dy = 0; dy < s->size; dy++)
        for (dx = 0; dx < s->size; dx++) {
            int j = (s->y + dy) * MW + s->x + dx;
            if (c_ref[j] == si) {
                c_ref[j] = 0;
                c_bt[j] = 0;
                c_zr[j] = (uint8_t)((c_zr[j] & 7u) | 2u << 4);
            }
        }
    {
        demo_t *d = &c_demo[c_demo_w % 16u];
        d->x = s->x;
        d->y = s->y;
        d->size = s->size;
        d->cause = (uint8_t)cause;
        c_demo_w++;
    }
}

static int redevelop(void)
{
    int k;
    for (k = 0; k < 12; k++) {
        int t = 1 + irnd(3);
        st_t *s;
        if (!C.nbl[t])
            continue;
        s = &c_st[c_ref[c_bl[t][irnd(C.nbl[t])]]];
        if (!s->type || (s->flags & SF_DEAD) || C.tick - s->born < 60u || !misfit(s))
            continue;
        if ((s->flags & SF_FARM) && !FIRST(FI_FARMGONE)) {
            SETFIRST(FI_FARMGONE);
            news(N_FARMGONE, 0, 0);
        } else if (!(s->flags & SF_FARM) && !FIRST(FI_REDEV)) {
            SETFIRST(FI_REDEV);
            news(N_REDEV, 0, 0);
        }
        city_demolish((int)(s - c_st), 0);
        return 1;
    }
    return 0;
}

int city_sweep(void)
{
    int i, n = 0;
    for (i = 0; i < MN; i++) {
        int r = RUBBLE(i);
        if (r == 1) {
            c_zr[i] &= 7u;
            n++;
        } else if (r == 2) {
            c_zr[i] = (uint8_t)((c_zr[i] & 7u) | 1u << 4);
        }
    }
    return n;
}

static int try_consolidate(int si)
{
    st_t *s = &c_st[si];
    int opts[4][2] = {{s->x - 1, s->y - 1}, {s->x, s->y - 1}, {s->x - 1, s->y}, {s->x, s->y}}, start = irnd(4), o, dx, dy;
    for (o = 0; o < 4; o++) {
        int ax = opts[(start + o) % 4][0], ay = opts[(start + o) % 4][1], ok = 1;
        if (ax < 1 || ay < 1 || ax + 1 >= MW - 1 || ay + 1 >= MH - 1)
            continue;
        for (dy = 0; dy < 2 && ok; dy++)
            for (dx = 0; dx < 2; dx++) {
                int x = ax + dx, y = ay + dy, j = y * MW + x;
                const st_t *o2;
                if (x == s->x && y == s->y)
                    continue;
                if (c_terr[j] & F_EMPTY)
                    continue;
                o2 = c_ref[j] ? &c_st[c_ref[j]] : 0;
                if (o2 && o2->type != BT_CIV && o2->size == 1 && o2->L <= 2 && (o2->type == s->type || rnd() < 0.5f))
                    continue;
                ok = 0;
                break;
            }
        if (!ok)
            continue;
        for (dy = 0; dy < 2; dy++)
            for (dx = 0; dx < 2; dx++) {
                int j = (ay + dy) * MW + ax + dx, oi = c_ref[j];
                if (oi && oi != si) {
                    st_t *o2 = &c_st[oi];
                    add_jobs(o2->type, -cap_of(o2->type, o2->L, 1));
                    if (o2->flags & SF_FARM)
                        C.farm_jobs -= cap_of(o2->type, o2->L, 1);
                    o2->flags |= SF_DEAD;
                    C.buildings--;
                }
            }
        add_jobs(s->type, cap_of(s->type, s->L, 2) - cap_of(s->type, s->L, 1));
        s->x = (uint8_t)ax;
        s->y = (uint8_t)ay;
        s->size = 2;
        set_foot(si);
        return 1;
    }
    return 0;
}

static int upgrade(int type)
{
    int k, samples, maxlv = max_lv_now(), best = 0, L;
    float bs = -1e9f;
    st_t *s;
    if (!C.nbl[type])
        return 0;
    samples = C.nbl[type] < 60 ? C.nbl[type] : 60;
    for (k = 0; k < samples; k++) {
        int si = c_ref[c_bl[type][irnd(C.nbl[type])]];
        const st_t *c = &c_st[si];
        float lv, sc;
        if (!si || (c->flags & (SF_DEAD | SF_FARM)) || c->type != type)
            continue;
        lv = LVF(c->y * MW + c->x);
        if (c->L >= maxlv || (type != BT_IND && lv < REQ[c->L]))
            continue;
        sc = (type == BT_IND ? (1.0f - lv) * 0.3f : lv) + rnd() * 0.25f - (float)c->L * 0.05f;
        if (sc > bs) {
            bs = sc;
            best = si;
        }
    }
    if (!best)
        return 0;
    s = &c_st[best];
    L = s->L;
    add_jobs(type, cap_of(type, L + 1, s->size) - cap_of(type, L, s->size));
    s->L = (uint8_t)(L + 1);
    s->born = C.tick;
    if ((type == BT_RES || type == BT_COM) && s->L == 4) {
        C.towers++;
        news(N_TOWER, type, 0);
    }
    if ((type == BT_COM || type == BT_IND) && s->L >= 3 && s->size == 1 && rnd() < 0.8f)
        try_consolidate(best);
    note_first(s);
    return 1;
}

/* --------------------------------------------------------- landmarks --- */
static int near_kind(int x, int y, int kind, float r)
{
    int k, n = 0;
    for (k = 0; k < C.nciv; k++) {
        const st_t *c = &c_st[c_civ[k]];
        if (c->kind == kind && hyp((float)(c->x - x), (float)(c->y - y)) < r)
            n++;
    }
    return n;
}
static int near_green(int x, int y, float r)
{
    int k, n = 0;
    for (k = 0; k < C.nciv; k++) {
        const st_t *c = &c_st[c_civ[k]];
        if (civ_green(c->kind) && hyp((float)(c->x - x), (float)(c->y - y)) < r)
            n++;
    }
    return n;
}
static int water_edge(int x, int y, int n)
{
    int w = 0, k;
    for (k = -1; k <= n; k++) {
        int p[4][2] = {{x + k, y - 1}, {x + k, y + n}, {x - 1, y + k}, {x + n, y + k}}, e;
        for (e = 0; e < 4; e++)
            if (in_map(p[e][0], p[e][1]) && TERR(p[e][1] * MW + p[e][0]) == T_WATER)
                w++;
    }
    return w;
}
static float special_score(int kind, int x, int y, int n)
{
    int i = y * MW + x;
    float lv = LVF(i), d = hyp((float)(x - C.cx), (float)(y - C.cy)), r = rnd();
    switch (kind) {
    case CV_TOWNHALL: return -d;
    case CV_GAS: return -fm_fabsf(lv - 0.45f) - (float)near_kind(x, y, kind, 12) * 2.0f + r * 0.2f;
    case CV_FIRE: return -fm_fabsf(lv - 0.5f) - (float)near_kind(x, y, kind, 14) * 2.0f + r * 0.2f;
    case CV_POLICE: return -fm_fabsf(lv - 0.55f) - (float)near_kind(x, y, kind, 14) * 2.0f + r * 0.2f;
    case CV_SCHOOL: return -fm_fabsf(lv - 0.45f) - (float)near_kind(x, y, kind, 14) * 2.0f + r * 0.3f;
    case CV_HOSPITAL: return -fm_fabsf(lv - 0.5f) - (float)near_kind(x, y, kind, 16) * 2.0f + r * 0.3f;
    case CV_STATION: return lv - (float)near_kind(x, y, kind, 14) * 2.0f + r * 0.2f;
    case CV_STADIUM: return -fm_fabsf(lv - 0.25f) + r * 0.3f;
    case CV_PARK: return -fm_fabsf(lv - 0.5f) * 0.6f - (float)near_green(x, y, 6) * 0.6f + r * 0.4f;
    case CV_POCKET: return lv - (float)near_green(x, y, 5) * 0.5f + r * 0.3f;
    case CV_PLAZA: return lv * 1.2f - (float)near_kind(x, y, kind, 8) * 1.5f + r * 0.2f;
    case CV_PLAYGROUND: return -fm_fabsf(lv - 0.4f) - (float)near_green(x, y, 5) * 0.4f + (float)neigh(x, y, 2).r * 0.04f + r * 0.3f;
    case CV_GARDEN: return -fm_fabsf(lv - 0.35f) - (float)near_green(x, y, 5) * 0.4f + r * 0.3f;
    case CV_POND: return -fm_fabsf(lv - 0.4f) - (float)near_green(x, y, 6) * 0.5f + r * 0.4f;
    case CV_FIELDS: return -fm_fabsf(lv - 0.25f) - (float)near_kind(x, y, kind, 14) * 2.0f + r * 0.3f;
    case CV_CEMETERY: return -fm_fabsf(lv - 0.2f) - (float)near_kind(x, y, kind, 20) * 3.0f + r * 0.2f;
    case CV_GOLF: return -lv - (float)near_kind(x, y, kind, 30) * 5.0f + r * 0.2f;
    case CV_MARINA: {
        int w = water_edge(x, y, n);
        return w >= 3 ? 1.0f + (float)w * 0.1f - (float)near_kind(x, y, kind, 16) * 2.0f + r : -1e9f;
    }
    }
    return 0.0f;
}

static int place_special(int kind)
{
    const civic_t *d = &CIVIC[kind];
    int n = d->size, best = -1, k, si, x, y;
    float bs = -1e9f;
    st_t *s;
    for (k = 0; k < C.nempty; k++) {
        int i = c_empty[k];
        float sc;
        x = i % MW;
        y = i / MW;
        if (n > 1 && !free_n(x, y, n))
            continue;
        sc = special_score(kind, x, y, n);
        if (sc > bs) {
            bs = sc;
            best = i;
        }
    }
    if (best < 0 || bs < -1e8f || C.nciv >= 128)
        return 0;
    si = st_alloc();
    if (!si)
        return 0;
    s = &c_st[si];
    s->type = BT_CIV;
    s->kind = (uint8_t)kind;
    s->L = 1;
    s->x = (uint8_t)(best % MW);
    s->y = (uint8_t)(best / MW);
    s->size = (uint8_t)n;
    s->born = C.tick;
    set_foot(si);
    c_civ[C.nciv++] = (uint16_t)si;
    C.jobs_civ += d->jobs;
    C.buildings++;
    if (civ_green(kind))
        C.greens++;
    if (kind == CV_PLAZA && !FIRST(FI_SQUARE)) {
        SETFIRST(FI_SQUARE);
        news(N_SQUARE, kind, 0);
    } else if (!civ_green(kind)) {
        news(N_CIVIC, kind, 0);
    } else if (!FIRST(FI_GREEN0 + kind - CV_GREEN0)) {
        news(N_GREEN, kind, 0);
    }
    if (civ_green(kind))
        SETFIRST(FI_GREEN0 + kind - CV_GREEN0);
    return 1;
}

static int count_kind(int kind)
{
    int k, n = 0;
    for (k = 0; k < C.nciv; k++)
        n += c_st[c_civ[k]].kind == kind;
    return n;
}
static int pick_green(void)
{
    int w[10], k, tot = 0, r;
    if (!FIRST(FI_SQUARE))
        return CV_PLAZA;
    for (k = 0; k < 10; k++)
        w[k] = GREENS[C.stage][k];
    if (!C.has_coast && !C.has_river)
        w[CV_MARINA - CV_GREEN0] = 0;
    if (w[CV_GOLF - CV_GREEN0] && count_kind(CV_GOLF) >= C.stage - 1)
        w[CV_GOLF - CV_GREEN0] = 0;
    if (w[CV_CEMETERY - CV_GREEN0] && count_kind(CV_CEMETERY) >= 2)
        w[CV_CEMETERY - CV_GREEN0] = 0;
    if (w[CV_MARINA - CV_GREEN0] && count_kind(CV_MARINA) >= 2)
        w[CV_MARINA - CV_GREEN0] = 0;
    for (k = 0; k < 10; k++)
        tot += w[k];
    r = irnd(tot);
    for (k = 0; k < 10; k++) {
        r -= w[k];
        if (r < 0)
            return CV_GREEN0 + k;
    }
    return CV_PARK;
}

/* ------------------------------------------------------------- a week --- */
static void check_stage(void)
{
    while (C.stage < 3 && C.pop >= STAGE_MIN[C.stage + 1]) {
        C.stage++;
        news(N_STAGE, C.stage, 0);
    }
}

void city_new(uint32_t seed)
{
    int o, d, e, x, y;
    static const int8_t D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    memset(&C, 0, sizeof C);
    memset(c_terr, 0, MN);
    memset(c_fl2, 0, MN);
    memset(c_road, 0, MN);
    memset(c_plan, 0, MN);
    memset(c_lm, 0, MN);
    memset(c_bt, 0, MN);
    memset(c_lv, 0, MN);
    memset(c_zr, 0, MN);
    memset(c_ref, 0, sizeof c_ref);
    memset(c_st, 0, sizeof c_st);
    C.seed = seed;
    C.rs = seed;
    C.next_green = 10;
    city_name(seed, C.name);
    gen_terrain();
    pick_center();
    gen_plan();
    C.minx = C.maxx = C.cx;
    C.miny = C.maxy = C.cy;
    for (o = 0; o < 2; o++)
        for (e = 0; e < 4; e++)
            for (d = 0; d <= 6; d++) {
                int i;
                x = C.cx + D[e][0] * d + (D[e][1] ? o : 0);
                y = C.cy + D[e][1] * d + (D[e][0] ? o : 0);
                if (x < 1 || y < 1 || x >= MW - 1 || y >= MH - 1)
                    break;
                i = y * MW + x;
                if (TERR(i) == T_WATER || !c_plan[i])
                    break;
                if (!c_road[i])
                    add_road(i, 0);
            }
    scan();
    compute_lv();
    compute_zones();
    news(N_FOUNDED, 0, 0);
}
int city_ready(void) { return C.seed != 0 || C.roads != 0; }

void city_step(void)
{
    static const float CR[4] = {0.3f, 0.32f, 0.4f, 0.48f}, IR[4] = {0.3f, 0.24f, 0.16f, 0.1f}, RED[4] = {0, 0.05f, 0.1f, 0.14f},
                       FARM[4] = {0.22f, 0.09f, 0.025f, 0.0f};
    int32_t jobs;
    float norm, w1, w2, w3, r;
    int type, farm, did = 0;
    C.tick++;
    scan();
    if (C.tick % 12u == 1u)
        compute_lv();
    if (C.tick % 26u == 2u)
        compute_zones();
    if (C.stage >= 1 && rnd() < RED[C.stage])
        redevelop();
    if (C.milestone < NMILE && C.pop >= MILESTONES[C.milestone].pop) {
        if (place_special(MILESTONES[C.milestone].kind)) {
            C.milestone++;
            C.m_fails = 0;
            return;
        }
        extend_roads(3);
        if (++C.m_fails > 40) {
            C.milestone++;
            C.m_fails = 0;
        }
    }
    if (C.dev >= C.next_green) {
        C.next_green += 11 + C.stage * 6;
        if (place_special(pick_green()))
            return;
    }
    jobs = C.jobs_c + C.jobs_i + C.jobs_civ;
    norm = (float)C.pop + 60.0f;
    C.dem_r = ((float)jobs * 2.0f + 30.0f - (float)C.pop) / norm;
    C.dem_c = ((float)C.pop * CR[C.stage] - (float)C.jobs_c) / norm;
    C.dem_i = ((float)C.pop * IR[C.stage] - (float)C.jobs_i) / norm;
    w1 = fm_maxf(0.03f, C.dem_r);
    w2 = fm_maxf(0.01f, C.dem_c);
    w3 = fm_maxf(0.01f, C.dem_i);
    w1 *= fm_sqrtf(w1);
    w2 *= fm_sqrtf(w2);
    w3 *= fm_sqrtf(w3);
    r = rnd() * (w1 + w2 + w3);
    type = r < w1 ? BT_RES : r < w1 + w2 ? BT_COM : BT_IND;
    if ((float)C.nempty < 12.0f + (float)C.pop / 450.0f)
        extend_roads(1 + C.stage * 2);
    farm = type == BT_IND && (float)C.farm_jobs < (float)C.pop * FARM[C.stage] + (C.stage == 0 ? 20.0f : 0.0f);
    if (!farm && rnd() < STAGE_UPG[C.stage])
        did = upgrade(type);
    if (!did)
        did = build(type, farm);
    if (!did && farm)
        did = build(type, 0);
    if (!did)
        did = upgrade(type);
    if (!did && !extend_roads(4)) {
        did = upgrade(BT_RES) || upgrade(BT_COM) || upgrade(BT_IND);
        if (!did && !C.done && !C.nempty) {
            C.done = 1;
            news(N_BUILTOUT, 0, 0);
        }
    }
    check_stage();
}
