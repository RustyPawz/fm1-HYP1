/* SPDX-License-Identifier: GPL-3.0-only */
/* BIT-1 project: the device's settings in one flash object (OBJ_PROJ), and the five songs each in their own
 * (OBJ_SONG0 + i). A song never saved, or saved in another format, comes as its recipe (en_song_recipe).
 * Part of the unity build, after app.h.
 *
 * The song open lives in en_song: the screen edits it, the engine plays it. Song parameters are written here
 * (en_song.par) and sent to the engine; the engine keeps its own copy. */
project_t proj;

int par_get(int p)
{
    if (p >= 0 && p < P_NSONG)
        return en_song.par[p];
    if (p >= P_NSONG && p < P_NPARAMS)
        return proj.dev[p - P_NSONG];
    return 0;
}

void par_put(int p, int v)
{
    const en_param_t *pi = en_param_info(p);
    if (p < 0 || p >= P_NPARAMS)
        return;
    v = v < pi->lo ? pi->lo : v > pi->hi ? pi->hi : v;
    if (p < P_NSONG)
        en_song.par[p] = (int16_t)v;
    else
        proj.dev[p - P_NSONG] = (int16_t)v;
    en_set(p, v);
}

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < NDEV; i++)
        proj.dev[i] = en_param_info(P_NSONG + i)->def;
    proj.song = 0;
    proj.oct = 0;
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT || proj.song >= EN_NSONG || proj.oct < -3 || proj.oct > 3)
        return 0;
    for (i = 0; i < NDEV; i++) {
        const en_param_t *p = en_param_info(P_NSONG + i);
        if (proj.dev[i] < p->lo || proj.dev[i] > p->hi)
            return 0;
    }
    return 1;
}

static en_song_t song_buf;              /* a song read from flash, checked before it goes in */

static int song_load(int i, en_song_t *s)       /* 0: song i from flash */
{
    int n;
    memset(s, 0, sizeof *s);
    n = plat_store_load(OBJ_SONG0 + (uint32_t)i, s, sizeof *s);
    return n == (int)sizeof *s && en_song_valid(s) ? 0 : -1;
}

int song_saved(int i) { return i >= 0 && i < EN_NSONG && song_load(i, &song_buf) == 0; }

void song_open(int i)
{
    i = (i % EN_NSONG + EN_NSONG) % EN_NSONG;
    en_stop();
    if (song_load(i, &song_buf) != 0)
        en_song_recipe(i, &song_buf);
    en_song = song_buf;
    proj.song = (uint8_t)i;
    en_reload();
}

void song_recipe(void)
{
    en_stop();
    en_song_recipe(proj.song, &song_buf);
    en_song = song_buf;
    en_reload();
}

int project_load(void)
{
    static project_t buf;
    int n;
    memset(&buf, 0, sizeof buf);
    n = plat_store_load(OBJ_PROJ, &buf, sizeof buf);
    proj = buf;
    if (n < 8 || !project_valid()) {
        project_defaults();
        song_open(0);
        return -1;
    }
    song_open(proj.song);
    return 0;
}

int project_save(void)
{
    int rc = plat_store_save(OBJ_SONG0 + proj.song, &en_song, sizeof en_song);
    if (rc == 0)
        rc = plat_store_save(OBJ_PROJ, &proj, sizeof proj);
    return rc;
}

void project_apply(void)
{
    int i;
    for (i = 0; i < NDEV; i++)
        en_set(P_NSONG + i, proj.dev[i]);
    en_reload();
}
