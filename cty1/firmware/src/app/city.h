/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1's city: a map grown from a seed (city.c) and its life (life.c). Main loop only: the music reads what
 * it needs through en_city and en_event (amb.h). Big arrays live in the .pool RAM region on the device.
 *
 * THE MAP     96 x 80 tiles: land, water (a coast, a river), woods and trees; a road plan drawn up at the
 *             founding (streets on blocks that grow larger outward, avenues with planted medians, roundabouts,
 *             cul-de-sacs, a beltway that opens when the town becomes a city); a zoning plan that follows the
 *             town as it grows (a commercial core, shopping strips on avenues, an industrial sector, farmland).
 * THE GROWTH  each week (a step) homes, shops and industry answer the demand (jobs against people), on lots
 *             their zone wants; land value (downtown, parks, water, minus industry) decides how tall; shops and
 *             industry absorb neighbours into 2 x 2 blocks; landmarks open at population milestones; a green
 *             space every dozen buildings; buildings out of place are cleared and rebuilt. Rubble (a fire, a
 *             quake, a clearance) stays until the next musical phrase after it settles. */
#pragma once
#include <stdint.h>

#ifndef EN_POOL
#define EN_POOL
#endif

#define MW 96
#define MH 80
#define MN (MW * MH)
#define NST 1536                        /* buildings, alive and free (slot 0 unused) */
#define NBL 1280                        /* a type's buildings, listed each week */

/* terrain (terr, low two bits) and the tile's flags (terr, high bits) */
enum { T_LAND, T_WATER, T_FOREST };
#define F_OCEAN 0x04u
#define F_TREE 0x08u
#define F_NWATER 0x10u
#define F_NOCEAN 0x20u
#define F_NROAD 0x40u
#define F_EMPTY 0x80u                   /* an open lot (scan) */
/* fl2 */
#define F2_STUB 0x01u                   /* a cul-de-sac's stub */
#define F2_NAV 0x02u                    /* near an avenue */
#define F2_NHWY 0x04u                   /* near the beltway */
#define F2_ROUND 0x08u                  /* a roundabout's tile */

/* road plan kinds (plan) and lanes (lm & 3: 0 crossing, 1 north-south, 2 east-west; lm >> 2: the median's sides) */
enum { PL_NONE, PL_STREET, PL_AVENUE, PL_BELTWAY, PL_CULDESAC };
/* zones (zr & 7) and rubble (zr >> 4: 2 fresh, 1 settled) */
enum { Z_NONE, Z_RES, Z_COM, Z_IND, Z_X, Z_FARM };
/* building types */
enum { BT_NONE, BT_RES, BT_COM, BT_IND, BT_CIV };
/* landmarks and green spaces */
enum { CV_TOWNHALL, CV_GAS, CV_FIRE, CV_POLICE, CV_SCHOOL, CV_HOSPITAL, CV_STATION, CV_STADIUM,
       CV_PARK, CV_POCKET, CV_PLAYGROUND, CV_GARDEN, CV_PLAZA, CV_POND, CV_FIELDS, CV_CEMETERY, CV_MARINA, CV_GOLF, CV_N };
#define CV_GREEN0 CV_PARK
#define SF_FARM 0x01u
#define SF_DEAD 0x02u
#define SF_BURN 0x04u

typedef struct {
    uint8_t type, L, x, y, size, kind, flags, pad;
    uint32_t born;                      /* the week it went up, or last grew */
    uint32_t motif;                     /* (ms) its motif may play again after */
} st_t;

typedef struct {
    uint32_t seed;
    char name[16];
    uint8_t cx, cy, has_coast, coast_side, has_river;
    float ind_angle;
    uint32_t tick, rs;                  /* weeks; the growth's random state */
    int32_t pop, jobs_c, jobs_i, jobs_civ, farm_jobs;
    uint8_t stage, milestone, m_fails, done;
    int32_t dev, next_green, greens, buildings, roads, towers;
    float dem_r, dem_c, dem_i;
    uint8_t minx, maxx, miny, maxy;     /* the town's extent (roads) */
    uint32_t firsts;                    /* the firsts said (FIRST_*) */
    uint32_t firsts2;                   /* a first of each type and level (bit type * 5 + L - 1), big too */
    uint16_t nempty, nbl[4];            /* open lots; buildings by type (anchors) */
    uint16_t nciv;
    uint16_t nrounds;
} city_t;
extern city_t C;

/* the map (pool) */
extern uint8_t c_terr[MN], c_fl2[MN], c_hgt[MN], c_road[MN], c_plan[MN], c_lm[MN], c_bt[MN], c_lv[MN], c_zr[MN];
extern uint16_t c_ref[MN];
extern st_t c_st[NST];
extern uint16_t c_empty[MN];             /* open lots this week */
extern uint16_t c_bl[4][NBL];            /* building anchors (tile index) by type, this week */
extern uint16_t c_scr[MN];               /* scratch: the plan's at the founding, then life's routes (BFS) */
extern uint16_t c_civ[128];              /* landmarks and green spaces (building slots) */
extern uint16_t c_round[64];             /* roundabouts: their top-left tile */

#define TERR(i) (c_terr[i] & 3u)
#define ZONE(i) (c_zr[i] & 7u)
#define RUBBLE(i) (c_zr[i] >> 4)
#define LANE(i) (c_lm[i] & 3u)
#define MED(i) (c_lm[i] >> 2)
#define LVF(i) ((float)c_lv[i] * 0.01f)
static inline int in_map(int x, int y) { return x >= 0 && y >= 0 && x < MW && y < MH; }

/* the city's news, in order: the screen's messages and the music's cues */
enum {
    N_FOUNDED = 1, N_FIRST, N_FIRSTBIG, N_CIVIC, N_GREEN, N_SQUARE, N_STAGE, N_BRIDGE, N_CAUSEWAY, N_BELTWAY, N_PORT,
    N_FARMS, N_FARMGONE, N_REDEV, N_BUILTOUT, N_BUILD, N_TOWER,
    /* life's */
    N_FIRE, N_BURNT, N_SAVED, N_QUAKE, N_ROBBERY, N_CAUGHT, N_ESCAPED, N_CRASH, N_TREATED, N_TOWED, N_UFO,
};
typedef struct { uint8_t code, a, b; } news_t;
#define NNEWS 32
extern news_t c_news[NNEWS];
extern uint32_t c_news_w;
void news(int code, int a, int b);

/* the growth */
void city_new(uint32_t seed);
void city_step(void);
int city_sweep(void);                    /* rubble: settled goes, fresh settles; returns how many cleared */
void city_demolish(int si, int cause);   /* cause: 0 redevelop, 1 burn, 2 quake */
int city_ready(void);                    /* founded */
const char *st_name(const st_t *s);      /* "House", "Shopping center", "Hospital" */
extern const char *const CIVIC_NAME[CV_N];
extern const char *const STAGE_NAME[4];
extern const int32_t STAGE_MIN[4];
uint32_t city_hash(uint32_t seed, int32_t x, int32_t y);
float city_hashf(uint32_t seed, int32_t x, int32_t y);
void city_name(uint32_t seed, char *out);  /* the name a seed gives its city */
int civ_green(int kind);
/* demolition events, for life's dust */
typedef struct { uint8_t x, y, size, cause; } demo_t;
extern demo_t c_demo[16];
extern uint32_t c_demo_w;
