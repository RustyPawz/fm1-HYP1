/* SPDX-License-Identifier: GPL-3.0-only */
/* HYP-1 in fm1-base's host simulator (base/host/plat_host.c, which includes app.c and so this): HYP-1's checks for
 * "expect", its command and a reboot, as host/amb_host.c had them on HYP-1's own simulator.
 *
 * Its checks: see below (fm1-base's own: store_writes clocks_out midi_out peak_db_max peak_db_min). Its commands:
 * seed N (the next power-on's seed; 0: the first piece, exact); splash MS (the next boot's RedtailWorks card).
 * Not carried over yet from the old simulator: frames and autopilot (the videos' tools), bank76, storefuzz. */

int app_expect(const char *what, int *got)
{
    int g;
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "frozen"))
        g = en_frozen;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "dd_changes"))          /* drum drift: patterns changed, mutations, bars left */
        g = (int)en_dd_changes;
    else if (!strcmp(what, "dd_mutations"))
        g = (int)en_dd_mutations;
    else if (!strcmp(what, "dd_left"))
        g = en_dd_left;
    else if (!strcmp(what, "pattern_synced"))
        g = proj.par[P_PATTERN] == en_pat_next;
    else if (!strcmp(what, "runner"))              /* the secret game (SEL + EDIT): on, lane, rows, gates, ... */
        g = kr.on;
    else if (!strcmp(what, "kr_lane"))
        g = kr.lane;
    else if (!strcmp(what, "kr_rows"))
        g = (int)kr.rows_born;
    else if (!strcmp(what, "kr_gates"))
        g = (int)kr.gates;
    else if (!strcmp(what, "kr_crashes"))
        g = (int)kr.crashes;
    else if (!strcmp(what, "kr_layer"))
        g = kr.layer;
    else if (!strcmp(what, "kr_over"))
        g = kr.over;
    else if (!strcmp(what, "piece"))
        g = proj.piece;
    else if (!strcmp(what, "pool"))               /* the scale, relative to the root (hex ok) */
        g = proj.pool;
    else if (!strcmp(what, "dirty"))              /* the bank has changes not yet in flash */
        g = project_pending();
    else if (!strcmp(what, "saved"))              /* the pieces in the bank (bit i: piece i) */
        g = (int)(project_saved(0) | project_saved(1) << 1 | project_saved(2) << 2 | project_saved(3) << 3 |
                    project_saved(4) << 4 | project_saved(5) << 5 | project_saved(6) << 6 | project_saved(7) << 7 |
                    project_saved(8) << 8 | project_saved(9) << 9);
    else if (!strcmp(what, "parsbad")) {          /* settings out of range: the playing piece's and every slot's */
        int p, i;
        g = 0;
        for (p = 0; p < P_NPARAMS; p++) {
            const en_param_t *pi = en_param_info(p);
            g += proj.par[p] < pi->lo || proj.par[p] > pi->hi;
            for (i = 0; i < NPIECES; i++)
                g += proj.slot[i].par[p] < pi->lo || proj.slot[i].par[p] > pi->hi;
        }
        for (i = 0; i < NPIECES; i++)
            g += !(proj.slot[i].pool & 1u) || (proj.slot[i].pool >> 12);
    } else if (!strcmp(what, "edited"))             /* the pieces changed since taken up (bit i: piece i) */
        g = proj.edited;
    else if (!strcmp(what, "sixteenths"))
        g = (int)en_bars;
    else if (!strcmp(what, "ext"))
        g = en_ext;
    else if (!strcmp(what, "pad_level"))
        g = (int)(en_pad_level * 100.0f);
    else if (!strcmp(what, "bass_level"))
        g = (int)(en_bass_level * 100.0f);
    else if (!strcmp(what, "chordroot"))
        g = en_chord_root;
    else if (!strcmp(what, "chordiv")) {          /* the pad's notes as intervals over the chord's root (bits) */
        int k;
        g = 0;
        for (k = 0; k < EN_NPAD; k++)
            g |= 1 << ((en_chord[k] - en_chord_root + 120) % 12);
    }
    else if (!strncmp(what, "chord", 5))          /* chordN: pad voice N's note */
        g = en_chord[atoi(what + 5) & 3];
    else if (!strcmp(what, "sec"))                /* the section: 0 groove, 1 breakdown, 2 build */
        g = en_sec;
    else if (!strcmp(what, "fxmask"))             /* the effects held (FX menu) */
        g = (int)ui.fx_mask;
    else if (!strcmp(what, "tempo"))
        g = proj.par[P_TEMPO];
    else if (!strcmp(what, "pat"))                /* the drum pattern playing (0..5) */
        g = en_pat_now;
    else if (!strcmp(what, "patnext"))            /* the one waiting for the bar */
        g = en_pat_next;
    else if (!strncmp(what, "lane", 4))           /* laneL: lane L's steps in the pattern chosen (bits) */
        g = (int)en_pats[proj.par[P_PATTERN]][atoi(what + 4) % EN_NLANE];
    else if (!strncmp(what, "pats", 4))           /* patsP_L: pattern P's lane L (bits) */
        g = (int)en_pats[atoi(what + 4) % EN_NPAT][atoi(strchr(what, '_') ? strchr(what, '_') + 1 : "0") % EN_NLANE];
    else if (!strcmp(what, "fxlock"))             /* the effects locked on (SAVE with their keys held) */
        g = (int)ui.fx_lock;
    else if (!strcmp(what, "fxon"))               /* the FX menu is on */
        g = ui.fx_menu;
    else if (!strcmp(what, "held"))               /* keys held playing a voice */
        g = en_held;
    else if (!strcmp(what, "remote"))             /* remote (SEL + GLO) on; the browser answering */
        g = rm.on;
    else if (!strcmp(what, "remotelink"))
        g = rm.link;
    else if (!strcmp(what, "lastout"))            /* the last MIDI out: status << 16 | d1 << 8 | d2 */
        g = (int)host_last_out;
    else if (!strcmp(what, "litbtn"))             /* the buttons lit (bit b: B_*) */
        g = (int)lit_btn;
    else if (!strcmp(what, "litkeys"))            /* the keys lit (bit k: key k) */
        g = (int)lit_keys;
    else if (!strcmp(what, "seqlane"))            /* the sequencer's lane (OCT- / OCT+) */
        g = ui.lane;
    else if (!strcmp(what, "acidnotes"))          /* the acid loop's steps that play */
        g = __builtin_popcount(en_acid_gate);
    else if (!strcmp(what, "theme"))
        g = proj.par[P_THEME];
    else if (!strncmp(what, "par", 3))          /* parN: parameter N (amb.h P_*) */
        g = proj.par[atoi(what + 3)];
    else
        return 0;
    *got = g;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "seed")) {                  /* seed N: the next power-on's (reboot) */
        host_seed = (uint32_t)strtoul(a, 0, 10);
        return 1;
    }
    if (!strcmp(cmd, "splash")) {                /* splash MS: the next boot's RedtailWorks card (0: none) */
        host_splash_ms = (uint32_t)strtoul(a, 0, 10);
        return 1;
    }
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }
