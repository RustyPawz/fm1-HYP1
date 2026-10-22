/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1 app: the project (everything that is saved) and the app's entry points. Included by the unity
 * build after gfx.c (drawing helpers in scope) and by the host simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../dsp/amb.h"

#ifndef EN_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION "DEV"
#endif
#define PROJ_MAGIC 0x31595443u           /* "CTY1" */
#define PROJ_FORMAT 2u                   /* 2: no preset cities; a new city each power-on */

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* everything (amb.h P_*); the seed is the city playing's, but each power-on
                                            brings a new one */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t rsv0[3];
    uint32_t boots;                      /* power-ons: mixed into each new city's seed */
    uint8_t rsv[24];                     /* room to grow */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */
void project_biome_music(int biome);     /* a biome's music: its tempo, flavor, lead, swing, echo, density, bed */
uint32_t project_seed(void);
void project_set_seed(uint32_t seed);

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
