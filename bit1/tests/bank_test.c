/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of firmware/src/bank.c through ota.c's own frame path: the frames the Voices page sends
 * (build/host/voices.frames, written by tests/host/voices_test.mjs from web/voices/voices.js) go in a SysEx at a
 * time, as from USB; the bank lands in a NOR kept in RAM (erase to FF, program only clears bits); every answer
 * is 0; what's in flash is the bank, header last; the engine plays from it. Then a send cut short leaves no
 * bank, and a frame out of order is refused.
 *   cc -DEN_HOST -Ifirmware/src/dsp -o bank_test tests/bank_test.c firmware/src/dsp/amb.c -lm
 *   bank_test build/host/voices.frames build/host/voices.bitv */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amb.h"

#define FELUCCA_ID "FM-1_900"
#define FELUCCA_OTA_DRYRUN 0

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st, (void)d1, (void)d2; }

static uint8_t nor[0x100000];
static uint8_t rx[1024];
static uint32_t rx_len, rx_full, fm1_ms, erases, bad_writes, answers, answer_bad;
static uint8_t last_answer[8];
static uint8_t flash_ok = 1;

/* ---- the device's surroundings ---- */
static void fm1_wdt_feed(void) { fm1_ms++; }
static void ui_say(const char *a, const char *b) { (void)a, (void)b; }
static const uint8_t *fm1_xip_ptr(uint32_t off) { return nor + off; }
static int st_erase(uint32_t off)
{
    if (off < 0xA0000u || off + 4096u > 0xDC000u || (off & 0xFFFu)) { bad_writes++; return -8; }
    memset(nor + off, 0xFF, 4096);
    erases++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off < 0xA0000u || off + n > 0xDC000u) { bad_writes++; return -8; }
    for (i = 0; i < n; i++) {
        if ((nor[off + i] & s[i]) != s[i])
            bad_writes++;                        /* a bit that would have to go 0 -> 1: not erased */
        nor[off + i] &= s[i];
    }
    return 0;
}

/* ---- ota.c's hooks ---- */
static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)(*in++ & 0x7Fu) << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}
static int ota_wire_send(const uint8_t *p, uint32_t n)   /* the device's answers */
{
    uint8_t u[64];
    uint32_t d = unpack7h(p + 1, n - 2, u);
    if (p[0] != 0xF0 || p[n - 1] != 0xF7 || d < 7 || u[0] != 0 || u[1] != 0x59) { answer_bad++; return 0; }
    answers++;
    memcpy(last_answer, u + 2, 7 < d - 2 ? 7 : d - 2);   /* cmd, len24, body... */
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!rx_full) return 0;
    *p = rx; *n = rx_len;
    return 1;
}
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_ms++; }
static int ota_erase(uint32_t off) { (void)off; return -8; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off, (void)p, (void)n; return -8; }
static int ota_fread(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step, (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }

static void bank_frame(const uint8_t *m, uint32_t d);
#define OTA_FRAME_HOOK(m, d) bank_frame(m, d)
#include "../firmware/src/ota.c"
#include "../firmware/src/bank.c"

static uint8_t *frames;
static long frames_n;
static int32_t blk[512];

/* one wire message F0..F7 into the device, as USB would hand it over; then the main loop's service */
static long feed(long at)
{
    long e = at;
    while (e < frames_n && frames[e] != 0xF7)
        e++;
    rx_len = (uint32_t)(e - at - 1);
    memcpy(rx, frames + at + 1, rx_len);
    rx_full = 1;
    ota_service();
    return e + 1;
}

static int fails;
static void check(const char *what, int ok)
{
    printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static uint8_t *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    if (!f) { printf("bank_test: no %s (run tests/host/voices_test.mjs first)\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)*n);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n) exit(1);
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    long bank_n, at, nframes = 0, bad_status = 0;
    uint8_t *bank;
    frames = slurp(argc > 1 ? argv[1] : "build/host/voices.frames", &frames_n);
    bank = slurp(argc > 2 ? argv[2] : "build/host/voices.bitv", &bank_n);
    en_init();
    memset(nor, 0x5A, sizeof nor);               /* what was there before: M-VAVE's samples, say */

    /* the whole send, as the page makes it */
    for (at = 0; at < frames_n;) {
        uint32_t a0 = answers;
        at = feed(at);
        nframes++;
        if (answers != a0 + 1 || last_answer[4] != 0)
            bad_status++;
    }
    en_render(blk, 256, 2048);
    check("every frame answered, each with 0", bad_status == 0 && answer_bad == 0);
    check("the bank's sectors erased, nothing written outside", erases == (uint32_t)((bank_n + 4095) / 4096) && bad_writes == 0);
    check("what's in flash is the bank", !memcmp(nor + 0xA0000, bank, (size_t)bank_n));
    check("END answers with the voices", last_answer[0] == 0x62 && last_answer[5] > 0);
    check("the engine plays from it", en_rom_count() == last_answer[5] && en_rom_count() > 0);
    printf("  (%ld frames, %ld bytes, %d voices)\n", nframes, bank_n, en_rom_count());

    /* a send cut short (no END): the old bank's gone, no half bank is played */
    {
        long cut = 0, k;
        for (at = 0, k = 0; at < frames_n && k < 5; k++)
            at = feed(at);
        cut = at;
        en_render(blk, 256, 2048);
        (void)cut;
        check("cut short: no bank played", en_rom_count() == 0);
        check("cut short: the header not written (no magic in flash)", nor[0xA0000] == 0xFF);
        bank_boot();                             /* as at the next power-on */
        en_render(blk, 256, 2048);
        check("cut short, then a power-on: still no bank", en_rom_count() == 0);
    }

    /* a frame out of order is refused; END with the bank incomplete doesn't check out */
    {
        uint8_t m[16] = {0, 0x59, 0x61, 9, 0, 0, 0x00, 0x30, 0, 0, 1, 2, 3, 4, 5, 0};
        uint32_t s = 0, i;
        for (at = 0, i = 0; i < 2; i++)
            at = feed(at);                       /* BEGIN and the first DATA */
        for (i = 6; i < 15; i++) s += m[i];
        m[15] = (uint8_t)~s;
        bank_frame(m, 16);                       /* offset 0x3000 next: not in order */
        check("out of order: refused", last_answer[0] == 0x61 && last_answer[4] == 3);
        memcpy(m, "\x00\x59\x62\x00\x00\x00\xFF", 7);
        bank_frame(m, 7);
        check("END too early: doesn't check out", last_answer[0] == 0x62 && last_answer[4] == 5);
    }
    printf(fails ? "bank: %d FAILED\n" : "bank: all ok\n", fails);
    return fails != 0;
}
