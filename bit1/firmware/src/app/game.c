/* SPDX-License-Identifier: GPL-3.0-only */
/* BIT-1's secret: BIT BEAT. Hold SEL and press REC (again, or HOME, to leave).
 *
 * The cursor's track becomes the chart: its notes fall down eight lanes, white keys 1-8, in time with the song
 * (its patterns' lengths, its repeats, its order list, read ahead as it plays). The track goes quiet and you play
 * it: a key on the line plays its note through the engine, on time or not at all. A kit's eight drums are the
 * eight lanes; a tune's notes go by their step in the scale, higher to the right. PERFECT within 50 ms, GOOD
 * within 110; the combo's multiplier climbs to x4. A miss shakes the screen, flashes it red and crunches the mix.
 *
 * The heat (the bar under the score) rises with every hit and falls with every miss, and the other tracks MUTATE
 * with it: Evolve comes in, then a melodic track starts to arpeggiate, then everything goes wild. All of it is
 * the engine's for now only: the song as saved never changes, and leaving puts every value back.
 *
 * At the end of each pass of the song: the score, the accuracy, the best combo and a grade; a white key plays on.
 * Included by ui.c: it draws with ui.c's helpers on the whole screen, two 240x120 halves a frame. */

#define GM_LANES 8
#define GM_HIT_Y 204                             /* the line */
#define GM_TOP_Y 34                              /* where the notes come in */
#define GM_FALL_MS 1300u                         /* from the top to the line */
#define GM_PERFECT 50
#define GM_GOOD 110
#define GM_RING 64                               /* rows remembered (by the engine's row count) */
#define GM_X0 12
#define GM_LW 27                                 /* a lane's width */

enum { GS_COUNT, GS_PLAY, GS_RESULT };

typedef struct {
    uint32_t id, t;                              /* the engine's row count when it plays; when (ms) */
    uint8_t lanes, hit, missed;                  /* the lanes with a note; those played; those missed */
    uint8_t note[GM_LANES];
} gm_row_t;

static struct {
    uint8_t on, st, trk, autoplay, lvl, lastpos, mutes, quit;
    uint32_t t0, row_t, rows_seen, keys, btn;
    uint32_t score, best, combo, maxc, perfect, good, miss, rounds;
    float heat;                                  /* 0..100 */
    uint32_t shake_until, red_until, lofi_until, text_until, flash_until[GM_LANES];
    char text[16];
    uint16_t text_col;
    uint8_t off_note;
    uint32_t off_at;
    gm_row_t ring[GM_RING];
    struct { int16_t x, y, vx, vy; uint8_t life; uint16_t c; } sp[24];
} gm;

static const uint16_t GM_LANE_C[GM_LANES] = {RGB(255, 64, 64), RGB(255, 140, 40), RGB(255, 200, 40), RGB(140, 230, 70),
                                             RGB(60, 220, 160), RGB(80, 200, 255), RGB(150, 120, 255), RGB(255, 90, 200)};

static int game_on(void) { return gm.on; }
static float gm_row_ms(void) { return 15000.0f / fm_maxf(en_bpm, 20.0f); }

/* -------------------------------------------------------------- the chart --- */
static int gm_lane(int note)                      /* a note's lane: a kit's drum, or its step in the scale */
{
    int st[12], n, root = par_get(P_ROOT), m = par_get(P_MODE), oct, pc, i, deg = 0;
    uint32_t pool = EN_MODE_POOL[(unsigned)m < EN_NMODES ? m : 0];
    if (en_sound_is_kit(par_get(P_T(gm.trk, TP_SOUND))))
        return (note - 36) & 7;
    note = en_snap(note, root, pool);
    n = en_scale(pool, st);
    oct = (note - root) / 12;
    pc = ((note - root) % 12 + 12) % 12;
    for (i = 0; i < n; i++)
        if (st[i] == pc)
            deg = i;
    deg += oct * n;
    return ((deg % GM_LANES) + GM_LANES) % GM_LANES;
}

/* what the chart track plays in a pattern's row: its cell, or (following the lead) its take */
static const en_cell_t *gm_cell(int pat, int row)
{
    static en_cell_t take[EN_NROW];
    static int take_pat = -1;
    static uint32_t take_seed;
    if (!en_follow_role(&en_song, gm.trk))
        return &en_song.cell[pat][row][gm.trk];
    if (take_pat != pat || take_seed != en_take_seed(&en_song, gm.trk, pat)) {
        take_seed = en_take_seed(&en_song, gm.trk, pat);
        en_follow(&en_song, pat, gm.trk, take_seed, take);
        take_pat = pat;
    }
    return &take[row];
}

/* the rows ahead, from the one playing: each into the ring (by its row count), when it plays */
static void gm_look(uint32_t now)
{
    const float rl = gm_row_ms();
    const int len = par_get(P_LEN) < 1 ? 1 : par_get(P_LEN), loop_pat = par_get(P_LOOP);
    const float swing = (float)par_get(P_SWING) * 0.01f * 0.5f * rl;
    int pos = en_pos % EN_NORD, row = en_row, rep = en_rep, k;
    const int ahead = (int)((float)GM_FALL_MS / rl) + 3;
    for (k = 0; k <= ahead && k < GM_RING - 8; k++) {
        const int pat = EN_ORD_PAT(en_song.order[pos]);
        const uint32_t id = gm.rows_seen - 1u + (uint32_t)k;
        gm_row_t *R = &gm.ring[id % GM_RING];
        const en_cell_t *c = gm_cell(pat, row);
        if (R->id != id) {                       /* a row new to the ring: its notes */
            R->id = id, R->lanes = R->hit = R->missed = 0;
            if (c->note >= NOTE_LO) {
                int l = gm_lane(c->note);
                R->lanes = (uint8_t)(1u << l);
                R->note[l] = c->note;
            }
        }
        R->t = gm.row_t + (uint32_t)((float)k * rl + (row & 1 ? swing : 0.0f));
        if (++row >= en_pat_len(&en_song, pat)) {   /* the next row: the pattern's end, its repeats, the list */
            row = 0;
            if (!loop_pat && ++rep >= EN_ORD_REP(en_song.order[pos])) {
                rep = 0;
                pos = (pos + 1) % len;
            }
        }
    }
}

/* ------------------------------------------------------------ the mutation --- */
/* the heat's level (0..3) sets the other tracks' Evolve, Arp and Vibrato, in the engine only */
static void gm_mutate(int lvl)
{
    int t, k = 0;
    if (lvl == gm.lvl)
        return;
    gm.lvl = (uint8_t)lvl;
    for (t = 0; t < EN_NTRK; t++) {
        const int kit = en_sound_is_kit(par_get(P_T(t, TP_SOUND)));
        int ev = par_get(P_T(t, TP_EVOLVE)), arp = par_get(P_T(t, TP_ARP)), vib = par_get(P_T(t, TP_VIB));
        if (t == gm.trk)
            continue;
        if (lvl >= 1)
            ev = ev > 45 ? ev : 45;
        if (lvl >= 2) {
            ev = ev > 70 ? ev : 70;
            if (!kit && !arp && k++ == 0)
                arp = 3;                         /* the first melodic track: up and down */
        }
        if (lvl >= 3) {
            ev = 100;
            if (!kit)
                arp = arp ? arp : 1 + t % 5, vib = vib > 60 ? vib : 60;
        }
        en_set(P_T(t, TP_EVOLVE), ev);
        en_set(P_T(t, TP_ARP), arp);
        en_set(P_T(t, TP_VIB), vib);
    }
    en_set(P_ECHO_FB, lvl >= 3 ? 70 : par_get(P_ECHO_FB));
}

/* -------------------------------------------------------------- the play --- */
static void gm_spark(int lane, uint16_t c, int n)
{
    int i, k = 0;
    for (i = 0; i < 24 && k < n; i++)
        if (!gm.sp[i].life) {
            gm.sp[i].x = (int16_t)(GM_X0 + lane * GM_LW + GM_LW / 2);
            gm.sp[i].y = GM_HIT_Y;
            gm.sp[i].vx = (int16_t)((int)((plat_ms() * 7u + (uint32_t)i * 13u) % 9u) - 4);
            gm.sp[i].vy = (int16_t)(-3 - (int)((plat_ms() + (uint32_t)i * 5u) % 4u));
            gm.sp[i].life = 14, gm.sp[i].c = c;
            k++;
        }
}
static void gm_say(const char *s, uint16_t c, uint32_t ms)
{
    uint32_t i;
    for (i = 0; i < sizeof gm.text - 1u && s[i]; i++)
        gm.text[i] = s[i];
    gm.text[i] = 0;
    gm.text_col = c;
    gm.text_until = plat_ms() + ms;
}
static void gm_heat(float d)
{
    gm.heat = fm_clampf(gm.heat + d, 0.0f, 100.0f);
    {
        const int lvl = gm.heat >= 85.0f ? 3 : gm.heat >= 55.0f ? 2 : gm.heat >= 25.0f ? 1 : 0;
        if (lvl > gm.lvl) {
            static const char *const MUT[4] = {"", "MUTATION 1", "MUTATION 2", "MUTATION MAX"};
            gm_say(MUT[lvl], K_COLD, 900u);
        }
        gm_mutate(lvl);
    }
}

static void gm_hit(gm_row_t *R, int l, int32_t dt)
{
    const int perfect = (dt < 0 ? -dt : dt) <= GM_PERFECT;
    const uint32_t mult = 1u + (gm.combo / 10u > 3u ? 3u : gm.combo / 10u);
    R->hit |= (uint8_t)(1u << l);
    if (gm.off_note)
        en_audition(gm.trk, gm.off_note, 0);
    en_audition(gm.trk, R->note[l], 1);          /* you play it */
    gm.off_note = R->note[l];
    gm.off_at = plat_ms() + (uint32_t)(gm_row_ms() * 1.6f);
    gm.combo++;
    if (gm.combo > gm.maxc)
        gm.maxc = gm.combo;
    gm.score += (perfect ? 300u : 120u) * mult;
    if (perfect)
        gm.perfect++;
    else
        gm.good++;
    gm.flash_until[l] = plat_ms() + 120u;
    gm_spark(l, perfect ? K_HOT : GM_LANE_C[l], perfect ? 6 : 3);
    gm_say(perfect ? "PERFECT" : "GOOD", perfect ? K_HOT : K_TEXT, 350u);
    gm_heat(perfect ? 4.0f : 2.0f);
}

static void gm_missed(void)
{
    gm.miss++;
    gm.combo = 0;
    gm.shake_until = gm.red_until = plat_ms() + 180u;
    gm.lofi_until = plat_ms() + 260u;
    en_set(P_LOFI, 90);                          /* the mix crunches */
    gm_say("MISS", K_ACC, 350u);
    gm_heat(-12.0f);
}

static void gm_press(int l, uint32_t now)
{
    gm_row_t *best = 0;
    int32_t bdt = 0x7FFFFFFF;
    int k;
    for (k = 0; k < GM_RING; k++) {
        gm_row_t *R = &gm.ring[k];
        int32_t dt = (int32_t)(now - R->t);
        if (!(R->lanes >> l & 1u) || ((R->hit | R->missed) >> l & 1u) || R->id + 8u < gm.rows_seen || R->id > gm.rows_seen + 8u)
            continue;
        if ((dt < 0 ? -dt : dt) <= GM_GOOD && (dt < 0 ? -dt : dt) < (bdt < 0 ? -bdt : bdt))
            best = R, bdt = dt;
    }
    gm.flash_until[l] = now + 80u;
    if (best)
        gm_hit(best, l, bdt);
}

static void game_start(void)
{
    uint32_t k;
    memset(&gm, 0, sizeof gm);
    gm.on = 1;
    gm.trk = ui.trk;
    gm.mutes = ui.mutes;
    gm.st = GS_COUNT;
    gm.t0 = plat_ms();
    for (k = 0; k < GM_RING; k++)
        gm.ring[k].id = 0xFFFFFFFFu;
    gm.keys = plat_keys();
    gm.btn = plat_buttons();
    en_stop();
    en_track_mute((uint32_t)ui.mutes | 1u << gm.trk);   /* the chart track is yours to play */
    ui.help = 0;
    lcd_fill(0, 0, 240, 240, K_BG);
}

static void game_stop(void)
{
    if (gm.off_note)
        en_audition(gm.trk, gm.off_note, 0);
    gm.on = 0;
    en_reload();                                 /* every value as the song has it */
    en_track_mute(gm.mutes);
    ui.sig[0] = ui.sig[1] = ui.sig[2] = 0;
    lcd_fill(0, 0, 240, 240, K_BG);
    say("BIT BEAT", gm.best ? "THANKS FOR PLAYING" : "BYE");
}

static void game_input(uint32_t btn, uint32_t keys)
{
    const uint32_t now = plat_ms(), down = keys & ~gm.keys;
    int l;
    gm.keys = keys;
    if (gm.st == GS_RESULT) {                    /* a white key: on to another round */
        for (l = 0; l < GM_LANES; l++)
            if (down >> WHITE_KEY[l] & 1u) {
                gm.st = GS_PLAY;
                gm.score = gm.combo = gm.maxc = gm.perfect = gm.good = gm.miss = 0;
                break;
            }
        return;
    }
    if (gm.st != GS_PLAY)
        return;
    (void)btn;
    for (l = 0; l < GM_LANES; l++)
        if (down >> WHITE_KEY[l] & 1u)
            gm_press(l, now);
}

/* the buttons and keys, read every millisecond between frames (the main loop's input-only calls): a key's
   press judged when it happens, not at the next frame; HOME, or SEL + REC, to leave */
static void game_poll(void)
{
    const uint32_t btn = plat_buttons(), keys = plat_keys(), down = btn & ~gm.btn;
    gm.btn = btn;
    if ((down >> B_HOME & 1u) || ((down >> B_REC & 1u) && (btn >> B_SEL & 1u)))
        gm.quit = 1;
    game_input(btn, keys);
}

static void game_update(void)
{
    const uint32_t now = plat_ms();
    int k, i;
    if (gm.st == GS_COUNT) {
        if (now - gm.t0 >= 2400u) {              /* 3, 2, 1: from the top */
            gm.st = GS_PLAY;
            en_play(0, 0);
            gm.rows_seen = en_rows;
            gm.lastpos = 0;
        }
        return;
    }
    if (en_rows != gm.rows_seen) {               /* a row has played: the time of it, the rows ahead */
        gm.rows_seen = en_rows;
        gm.row_t = now;
        if (en_pos < gm.lastpos && gm.st == GS_PLAY && (gm.perfect + gm.good + gm.miss) > 0) {
            gm.st = GS_RESULT;                   /* round the song: how it went */
            gm.rounds++;
            if (gm.score > gm.best)
                gm.best = gm.score;
        }
        gm.lastpos = en_pos;
    }
    if (gm.row_t)
        gm_look(now);
    for (k = 0; k < GM_RING; k++) {              /* past the line by more than GOOD: missed */
        gm_row_t *R = &gm.ring[k];
        uint8_t left = (uint8_t)(R->lanes & ~R->hit & ~R->missed);
        if (!left || R->id == 0xFFFFFFFFu)
            continue;
        if (gm.autoplay && gm.st == GS_PLAY) {   /* the demo: every note on time */
            for (i = 0; i < GM_LANES; i++)
                if ((left >> i & 1u) && (int32_t)(now - R->t) >= 0)
                    gm_hit(R, i, (int32_t)(now - R->t));
            left = (uint8_t)(R->lanes & ~R->hit & ~R->missed);
        }
        if (left && (int32_t)(now - R->t) > GM_GOOD) {
            R->missed |= left;
            if (gm.st == GS_PLAY)
                gm_missed();
        }
    }
    if (gm.off_note && (int32_t)(now - gm.off_at) >= 0) {
        en_audition(gm.trk, gm.off_note, 0);
        gm.off_note = 0;
    }
    if (gm.lofi_until && (int32_t)(now - gm.lofi_until) >= 0) {
        en_set(P_LOFI, par_get(P_LOFI));
        gm.lofi_until = 0;
    }
    gm_heat(-0.02f);                             /* it cools a little on its own */
    for (i = 0; i < 24; i++)
        if (gm.sp[i].life) {
            gm.sp[i].x = (int16_t)(gm.sp[i].x + gm.sp[i].vx);
            gm.sp[i].y = (int16_t)(gm.sp[i].y + gm.sp[i].vy);
            gm.sp[i].vy++;
            gm.sp[i].life--;
        }
}

/* --------------------------------------------------------------- drawing --- */
static void gm_draw(int y0)
{
    const uint32_t now = plat_ms();
    const int32_t sx = (int32_t)now < (int32_t)gm.shake_until ? ((now / 30u) & 1u ? 3 : -3) : 0;
    char b[24];
    int l, k, i;
    cv_begin(240, 120, K_BG);
    cv_oy = -y0, cv_y0 = 0, cv_y1 = 120;         /* draw in the screen's rows */
    for (l = 0; l < GM_LANES; l++) {             /* the lanes */
        const int32_t x = sx + GM_X0 + l * GM_LW;
        const int lit = (int32_t)(now - gm.flash_until[l]) < 0 || (gm.keys >> WHITE_KEY[l] & 1u);
        cv_rect(x, GM_TOP_Y, GM_LW - 1, GM_HIT_Y - GM_TOP_Y + 20, lit ? mix(K_BG, GM_LANE_C[l], 4) : mix(K_BG, K_PANEL, 10));
        cv_rect(x + GM_LW - 1, GM_TOP_Y, 1, GM_HIT_Y - GM_TOP_Y + 20, K_BG);
    }
    if (en_row % 4u == 0 && gm.st == GS_PLAY)    /* the beat: the line glows */
        cv_rect(sx + GM_X0, GM_HIT_Y - 2, GM_LANES * GM_LW, 1, K_HOT);
    cv_rect(sx + GM_X0, GM_HIT_Y, GM_LANES * GM_LW, 2, K_TEXT);
    for (k = 0; k < GM_RING; k++) {              /* the notes */
        const gm_row_t *R = &gm.ring[k];
        int32_t dt, y;
        if (!R->lanes || R->id == 0xFFFFFFFFu)
            continue;
        dt = (int32_t)(R->t - now);
        if (dt > (int32_t)GM_FALL_MS || dt < -300)
            continue;
        y = GM_HIT_Y - dt * (GM_HIT_Y - GM_TOP_Y) / (int32_t)GM_FALL_MS;
        for (l = 0; l < GM_LANES; l++) {
            if (!(R->lanes >> l & 1u) || (R->hit >> l & 1u))
                continue;
            cv_round(sx + GM_X0 + l * GM_LW + 2, y - 4, GM_LW - 5, 8, 3, R->missed >> l & 1u ? K_LINE : GM_LANE_C[l]);
        }
    }
    for (l = 0; l < GM_LANES; l++) {             /* the keys on the line */
        const int32_t x = sx + GM_X0 + l * GM_LW;
        const int down = gm.keys >> WHITE_KEY[l] & 1u;
        b[0] = (char)('1' + l), b[1] = 0;
        cv_round(x + 2, GM_HIT_Y + 5, GM_LW - 5, 14, 3, down ? GM_LANE_C[l] : K_PANEL);
        text_c(x + GM_LW / 2, GM_HIT_Y + 3, &FONT_XS, b, down ? K_BG : K_DIM);
    }
    for (i = 0; i < 24; i++)                     /* the sparks */
        if (gm.sp[i].life)
            cv_rect(sx + gm.sp[i].x, gm.sp[i].y, 3, 3, gm.sp[i].c);
    /* the score, the combo, the heat */
    itoa_u(gm.score, b);
    cv_text(6, -2, &FONT_B, b, K_TEXT);
    {
        const uint32_t mult = 1u + (gm.combo / 10u > 3u ? 3u : gm.combo / 10u);
        char c[16] = "x";
        itoa_u(mult, c + 1);
        cv_text(236 - text_w(&FONT_B, c), -2, &FONT_B, c, mult >= 4u ? K_HOT : K_ACC);
        if (gm.combo >= 5u) {
            itoa_u(gm.combo, c);
            text_c(120, 0, &FONT_S, c, K_TEXT);
        }
    }
    cv_rect(6, 24, 228, 4, K_PANEL);
    cv_rect(6, 24, (int32_t)(gm.heat * 2.28f), 4, gm.lvl >= 3 ? K_HOT : gm.lvl ? K_COLD : K_DIM);
    if ((int32_t)(now - gm.text_until) < 0)
        text_c(120 + sx, 120, &FONT_L, gm.text, gm.text_col);
    if ((int32_t)(now - gm.red_until) < 0) {     /* a miss: the edges go red */
        cv_rect(0, 0, 240, 3, K_ACC);
        cv_rect(0, 237, 240, 3, K_ACC);
        cv_rect(0, 0, 3, 240, K_ACC);
        cv_rect(237, 0, 3, 240, K_ACC);
    }
    if (gm.st == GS_COUNT) {
        const uint32_t n = 3u - (now - gm.t0) / 800u;
        cv_round(40, 70, 160, 100, 6, K_PANEL);
        text_c(120, 74, &FONT_B, "BIT BEAT", K_HOT);
        b[0] = (char)('0' + (n > 3u ? 3u : n)), b[1] = 0;
        text_c(120, 104, &FONT_L, b, K_TEXT);
        text_c(120, 146, &FONT_XS, "WHITE KEYS 1-8 ON THE LINE", K_DIM);
    } else if (gm.st == GS_RESULT) {
        const uint32_t n = gm.perfect + gm.good + gm.miss, acc = n ? (gm.perfect * 100u + gm.good * 60u) / n : 0;
        const char *grade = acc >= 95u ? "S" : acc >= 85u ? "A" : acc >= 70u ? "B" : acc >= 50u ? "C" : "D";
        char c[24];
        cv_round(24, 50, 192, 150, 6, K_PANEL);
        text_c(120, 52, &FONT_S, "ROUND THE SONG", K_DIM);
        text_c(120, 70, &FONT_L, grade, K_HOT);
        itoa_u(gm.score, c);
        text_c(120, 110, &FONT_B, c, K_TEXT);
        itoa_u(acc, c);
        for (k = 0; c[k]; k++)
            ;
        c[k] = '%', c[k + 1] = 0;
        cv_text(40, 136, &FONT_S, "ACCURACY", K_DIM);
        cv_text(200 - text_w(&FONT_S, c), 136, &FONT_S, c, K_TEXT);
        itoa_u(gm.maxc, c);
        cv_text(40, 154, &FONT_S, "BEST COMBO", K_DIM);
        cv_text(200 - text_w(&FONT_S, c), 154, &FONT_S, c, K_TEXT);
        text_c(120, 178, &FONT_XS, "A WHITE KEY: AGAIN  -  HOME: LEAVE", K_DIM);
    }
    cv_blit(0, (uint32_t)y0);
}

static void game_draw(void)
{
    uint32_t k = 0;
    int l;
    gm_draw(0);
    gm_draw(120);
    for (l = 0; l < GM_LANES; l++) {             /* the lights: a lane's key as its note comes near */
        int i;
        for (i = 0; i < GM_RING; i++) {
            const gm_row_t *R = &gm.ring[i];
            const int32_t dt = (int32_t)(R->t - plat_ms());
            if ((R->lanes >> l & 1u) && !((R->hit | R->missed) >> l & 1u) && dt < 220 && dt > -GM_GOOD)
                k |= 1u << WHITE_KEY[l];
        }
    }
    plat_glow(1);
    plat_leds(1u << B_REC, k);
}
