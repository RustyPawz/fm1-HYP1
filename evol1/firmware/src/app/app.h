/* SPDX-License-Identifier: GPL-3.0-only */
/* EVOL-1 app: the project (the device's settings, saved) and the songs (each its own flash object), and the
 * app's entry points. Included by the unity build after gfx.c (drawing helpers in scope) and by the host
 * simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../dsp/amb.h"

#ifndef EN_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION "DEV"
#endif
#define PROJ_MAGIC 0x314C5645u           /* "EVL1" */
#define PROJ_FORMAT 1u
#define NDEV (P_NPARAMS - P_NSONG)       /* the device's parameters: Tune, Sync, MIDI out, Channel, Theme, Lights */

typedef struct {
    uint32_t magic, format;
    int16_t dev[NDEV];                   /* P_NSONG + i */
    uint8_t song;                        /* the song open */
    int8_t oct;                          /* the keys' octave, -3..3 */
    uint8_t rsv[30];                     /* room to grow */
} project_t;

extern project_t proj;
_Static_assert(sizeof(en_song_t) <= PLAT_STORE_MAX, "a song must fit its flash object");
_Static_assert(EN_NSONG == PLAT_NSONG, "a flash object a song");

int par_get(int p);                      /* any parameter: the song's or the device's */
void par_put(int p, int v);              /* kept and sent to the engine (clamped) */
void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place (and song 1 as it comes) */
int project_save(void);                  /* the device's settings and the song open: 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */
void song_open(int i);                   /* song i from flash (or as it comes, if never saved) */
void song_recipe(void);                  /* the song open back to as it comes (not saved until SAVE) */
int song_saved(int i);                   /* 1: song i has been saved */

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
