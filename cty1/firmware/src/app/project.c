/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1 project: one flash object (OBJ_PROJ). A project of another format, or none, gives the defaults. Part
 * of the unity build, after app.h.
 *
 * The cities (PRESETS): ten seeds, each with a recipe for the music and the world. Each keeps its own settings
 * as you leave it (and its seed: EDIT gives it a new one); Tune, Sync, MIDI out and the screen are global. The
 * grown city itself isn't saved: a seed grows the same town, and Start as grows it back to a stage at once. */
#include "city.h"

project_t proj;

/* the recipes: a seed word (its FNV-1a hash), then the music and the world */
static const struct {
    const char *seed;
    int16_t tempo, flavor, lead, swing, density, bed, echo, mood, season, day, weather, speed, start;
} CITY[NPIECES] = {
    {"redtail", 104, 0, 0, 12, 60, 60, 100, 0, 0, 1, 0, 2, 0},     /* Brighton: the NES, the seasons going round */
    {"42", 96, 1, 2, 18, 55, 65, 110, 0, 0, 1, 0, 2, 1},           /* Cedarport: the Game Boy, a port town */
    {"maple", 112, 0, 1, 8, 65, 55, 90, 0, 2, 1, 0, 3, 0},         /* Faircrest: spring, quick */
    {"panda", 88, 1, 0, 24, 50, 70, 120, 1, 3, 2, 1, 2, 1},        /* Westhaven: summer, slow and bright */
    {"harbor", 100, 2, 3, 10, 60, 60, 100, 0, 0, 1, 0, 2, 2},      /* Stonedale: the SID, a city already */
    {"dusk", 84, 2, 2, 20, 45, 75, 140, 2, 4, 3, 5, 1, 1},         /* Stonegate: autumn fog, long nights, dark */
    {"ember", 120, 0, 3, 6, 70, 55, 80, 0, 3, 1, 4, 3, 1},         /* Ashcrest: summer storms */
    {"fern", 92, 1, 2, 16, 55, 65, 120, 0, 2, 2, 3, 2, 0},         /* Lakecrest: spring rain */
    {"nova", 132, 0, 1, 4, 75, 50, 70, 1, 0, 0, 0, 5, 0},          /* Belhaven: fast, growing fast */
    {"violet", 108, 2, 0, 14, 55, 60, 130, 0, 5, 2, 6, 2, 2},      /* Sunview: winter, snow on the city */
};

static char piece_names[NPIECES][16];
const char *piece_name(int i)
{
    const slot_t *s;
    uint32_t seed;
    i = (i % NPIECES + NPIECES) % NPIECES;
    s = i == proj.piece ? 0 : &proj.slot[i];
    seed = s ? (uint32_t)(uint16_t)s->par[P_SEED_HI] << 16 | (uint16_t)s->par[P_SEED_LO]
             : (uint32_t)(uint16_t)proj.par[P_SEED_HI] << 16 | (uint16_t)proj.par[P_SEED_LO];
    city_name(seed, piece_names[i]);
    return piece_names[i];
}

static int global_par(int p)
{
    return p == P_TUNE || p == P_MIDI || p == P_SYNC || p == P_MCH || p == P_THEME || p == P_ZOOM || p == P_MAP || p == P_LABELS ||
           p == P_FOLLOW || p == P_IDLE;
}

static void recipe(int i, slot_t *s)
{
    int p;
    uint32_t seed = en_hash_str(CITY[i].seed);
    for (p = 0; p < P_NPARAMS; p++)
        s->par[p] = en_param_info(p)->def;
    s->par[P_TEMPO] = CITY[i].tempo;
    s->par[P_FLAVOR] = CITY[i].flavor;
    s->par[P_LEAD] = CITY[i].lead;
    s->par[P_SWING] = CITY[i].swing;
    s->par[P_DENSITY] = CITY[i].density;
    s->par[P_BED] = CITY[i].bed;
    s->par[P_ECHO] = CITY[i].echo;
    s->par[P_MOOD] = CITY[i].mood;
    s->par[P_SEASON] = CITY[i].season;
    s->par[P_DAYLEN] = CITY[i].day;
    s->par[P_WEATHER] = CITY[i].weather;
    s->par[P_SPEED] = CITY[i].speed;
    s->par[P_START] = CITY[i].start;
    s->par[P_SEED_HI] = (int16_t)(uint16_t)(seed >> 16);
    s->par[P_SEED_LO] = (int16_t)(uint16_t)seed;
    s->pool = 0xAB5u;
}

static void take(const slot_t *s)                /* a city's settings into play, the globals kept */
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        if (!global_par(p))
            proj.par[p] = s->par[p];
    proj.pool = s->pool;
}

static void keep(void)                           /* the city playing, into its slot */
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        proj.slot[proj.piece].par[p] = proj.par[p];
    proj.slot[proj.piece].pool = proj.pool;
}

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < NPIECES; i++)
        recipe(i, &proj.slot[i]);
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = en_param_info(i)->def;
    take(&proj.slot[0]);
    proj.leds = 1;
}

static int pars_ok(const int16_t *par)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++) {
        const en_param_t *p = en_param_info(i);
        if (par[i] < p->lo || par[i] > p->hi)
            return 0;
    }
    return 1;
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT || proj.piece >= NPIECES || !pars_ok(proj.par))
        return 0;
    for (i = 0; i < NPIECES; i++)
        if (!pars_ok(proj.slot[i].par))
            return 0;
    return 1;
}

int project_load(void)
{
    static project_t buf;
    int n;
    memset(&buf, 0, sizeof buf);
    n = plat_store_load(OBJ_PROJ, &buf, sizeof buf);
    proj = buf;
    if (n >= 8 && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void)
{
    keep();
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
}

void project_piece(int i)
{
    keep();
    proj.piece = (uint8_t)((i % NPIECES + NPIECES) % NPIECES);
    take(&proj.slot[proj.piece]);
}

void project_piece_reset(void)
{
    slot_t s;
    recipe(proj.piece, &s);
    take(&s);
}

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, proj.par[i]);
}

uint32_t project_seed(void) { return (uint32_t)(uint16_t)proj.par[P_SEED_HI] << 16 | (uint16_t)proj.par[P_SEED_LO]; }
