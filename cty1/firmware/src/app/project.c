/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1 project: one flash object (OBJ_PROJ). A project of another format, or none, gives the defaults. Part
 * of the unity build, after app.h.
 *
 * No preset cities: each power-on founds a new one, on a new seed, in a biome (Random, or the one Biome asks
 * for). Its biome brings its own music (tempo, flavor, lead, swing, echo, density, bed); everything else is
 * yours and SAVE keeps it. The grown city itself isn't saved: SAVE elsewhere grows it again from its founding. */
#include "city.h"

project_t proj;

/* each biome's music (city.h B_*) */
static const struct {
    int16_t tempo, flavor, lead, swing, density, bed, echo;
} MUSIC[NBIOME] = {
    {96, 1, 2, 14, 60, 62, 115},    /* forest: the Game Boy's wave, woodblocks, a long echo */
    {88, 2, 3, 6, 50, 58, 95},      /* desert: the SID, a saw lead over a drone */
    {84, 0, 1, 8, 55, 60, 140},     /* alpine: the NES, thin and bright, echoing off the peaks */
    {112, 1, 2, 22, 65, 60, 90},    /* volcano isles: the wave like a steel drum, swung */
    {100, 0, 0, 10, 60, 60, 80},    /* plains: square and plain, boom-chick */
    {120, 0, 0, 18, 70, 64, 70},    /* beach: surf, quick, a short echo */
    {104, 1, 2, 16, 60, 60, 125},   /* river lands: flowing, pentatonic */
};

void project_biome_music(int biome)
{
    int b = (unsigned)biome < NBIOME ? biome : 0;
    proj.par[P_TEMPO] = MUSIC[b].tempo;
    proj.par[P_FLAVOR] = MUSIC[b].flavor;
    proj.par[P_LEAD] = MUSIC[b].lead;
    proj.par[P_SWING] = MUSIC[b].swing;
    proj.par[P_DENSITY] = MUSIC[b].density;
    proj.par[P_BED] = MUSIC[b].bed;
    proj.par[P_ECHO] = MUSIC[b].echo;
}

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = en_param_info(i)->def;
    proj.leds = 1;
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT)
        return 0;
    for (i = 0; i < P_NPARAMS; i++) {
        const en_param_t *p = en_param_info(i);
        if (proj.par[i] < p->lo || proj.par[i] > p->hi)
            return 0;
    }
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

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, proj.par[i]);
}

uint32_t project_seed(void) { return (uint32_t)(uint16_t)proj.par[P_SEED_HI] << 16 | (uint16_t)proj.par[P_SEED_LO]; }
void project_set_seed(uint32_t seed)
{
    proj.par[P_SEED_HI] = (int16_t)(uint16_t)(seed >> 16);
    proj.par[P_SEED_LO] = (int16_t)(uint16_t)seed;
    en_set(P_SEED_HI, proj.par[P_SEED_HI]);
    en_set(P_SEED_LO, proj.par[P_SEED_LO]);
}
