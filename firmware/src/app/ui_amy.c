/* SPDX-License-Identifier: GPL-3.0-only */
/* X0X_AMY's UI (included by ui.c, which hands it ui_init / ui_frame): the 128 Juno-60 presets on the
 * keys, and the AMYboard editor's patch knobs (amyboard.com/editor) on the FM-1's knobs.
 *
 * A preset loads as the editor's Load Preset loads it: AMY patch 257 (AMYboard's default, 4 oscs a
 * voice), then the editor's wire messages that set it up like the Juno patch (amy_juno.h, from
 * tools/amy_k257.json; tools/gen_amy_k257.js runs the editor's own translation). Turning PRESET
 * shows the preset at once and loads it when the knob has rested PRESET_REST_MS, so scrolling
 * through doesn't load every one on the way.
 *
 *   PRESET   the patch (A11 .. B88)         SELECT   the page: OSC A, OSC B, LFO, VCF, VCF ENV,
 *   K1..K4   the page's knobs                        ADSR, EQ, CHORUS, REVERB, ECHO, DISTORTION
 *   OCT- / OCT+   the keys' octave (-2 .. +2)
 *
 * The pages, their knobs, ranges, curves and the AMY messages they send are the editor's
 * (amy_parameters.js), so a knob here does what the same knob there does; the editor's LFO "wave"
 * selector is left out. Its channel is synth 1 here (%i), its oscs those of patch 257: 0 the filter
 * and VCA, 1 the LFO, 2 Osc A, 3 Osc B. A continuous knob is 0..127 steps
 * along its range (the editor's log curve where it has one); a selector steps through its options.
 * A knob keeps its exact value (a patch's 180 Hz stays 180 Hz) until it is turned, when it moves to
 * the step one detent from the nearest one.
 * Choosing a preset puts the knobs where its messages set them (build/gen/amy_juno.h,
 * tools/gen_amy_juno.py); the bus effects no preset sets (reverb, echo, distortion) keep theirs.
 * Echo is shown but does nothing: its delay line doesn't fit beside the reverb in the FM-1's RAM.
 * Nothing is saved: power-on is X0X_AMY_PATCH with its own values. */
#include <stddef.h>
#include <stdio.h>
#ifdef X0X_HOST
#include <string.h>
#else
char *strchr(const char *s, int c);     /* newlib; its string.h clashes with libc.c's memcpy (unsigned n) */
#endif
#include <math.h>
#include "amy_juno.h"

enum { KT_LIN, KT_LOG, KT_SEL };
typedef struct {
    const char *name;
    uint8_t type;
    float lo, hi, off;                 /* range; log knobs: (lo+off) * ((hi+off)/(lo+off))^t - off */
    const char *code;                  /* the editor's change_code: %i the synth, %v the value */
    uint8_t nopt;                      /* KT_SEL: its options' names, and their %v (values or codes) */
    const char *const *opt;
    const char *const *optv;
} knob_t;

static const char *const WAVE[] = {"SINE", "PULSE", "SAW UP", "SAW DOWN", "TRIANGLE", "NOISE", "PCM", "WAVETABLE", "ALGO"};
static const char *const WAVE_V[] = {"0", "1", "3", "2", "4", "5", "7", "19", "8"};
static const uint8_t WAVE_NUM[] = {0, 1, 3, 2, 4, 5, 7, 19, 8};   /* WAVE_V as numbers */
static const char *const DIST[] = {"OFF", "CLIP", "FOLD", "CLIP+FOLD", "CRUSH", "ALL"};
static const char *const DIST_V[] = {"GC0GF0GH0,0", "GC1GF0GH0,0", "GC0GF1GH0,0", "GC1GF1GH0,0", "GC0GF0GH6,5", "GC1GF1GH6,5"};
#define SEL(o, v) (uint8_t)(sizeof o / sizeof o[0]), o, v
#define NOSEL 0, 0, 0                  /* a continuous knob: no options */

/* in amy_juno.h's order (tools/gen_amy_juno.py NAMES) */
static const knob_t KN[] = {
    {"FREQ", KT_LOG, 50, 2000, 0, "i%iv2f%v", NOSEL},                 /* OSC A */
    {"WAVE", KT_SEL, 0, 0, 0, "i%iv2w%v", SEL(WAVE, WAVE_V)},
    {"DUTY", KT_LIN, 0.5f, 0.99f, 0, "i%iv2d%v", NOSEL},
    {"LEVEL", KT_LOG, 0.001f, 1, 0.1f, "i%iv2a%v", NOSEL},
    {"FREQ", KT_LOG, 50, 2000, 0, "i%iv3f%v", NOSEL},                 /* OSC B */
    {"WAVE", KT_SEL, 0, 0, 0, "i%iv3w%v", SEL(WAVE, WAVE_V)},
    {"DUTY", KT_LIN, 0.5f, 0.99f, 0, "i%iv3d%v", NOSEL},
    {"LEVEL", KT_LOG, 0.001f, 1, 0.1f, "i%iv3a%v", NOSEL},
    {"FREQ", KT_LOG, 0.1f, 20, 0, "i%iv1f%v", NOSEL},                 /* LFO */
    {"OSC", KT_LOG, 0, 4, 0.001f, "i%iv2f,,,,,%vZi%iv3f,,,,,%v", NOSEL},
    {"PWM", KT_LIN, 0, 0.49f, 0, "i%iv2d,,,,,%vZi%iv3d,,,,,%v", NOSEL},
    {"FILT", KT_LOG, 0, 4, 0.001f, "i%iv0F,,,,,%v", NOSEL},
    {"FREQ", KT_LOG, 20, 8000, 0, "i%iv0F%v", NOSEL},                 /* VCF */
    {"RESONANCE", KT_LOG, 0.5f, 16, 0, "i%iv0R%v", NOSEL},
    {"KBD", KT_LIN, 0, 1, 0, "i%iv0F,%v", NOSEL},
    {"ENV", KT_LIN, -10, 10, 0, "i%iv0F,,,,%v", NOSEL},
    {"ATTACK", KT_LIN, 0, 1000, 0, "i%iv0B%v,1,,,,0", NOSEL},         /* VCF ENV */
    {"DECAY", KT_LOG, 0, 2000, 50, "i%iv0B,1,%v,,,0", NOSEL},
    {"SUSTAIN", KT_LIN, 0, 1, 0, "i%iv0B,1,,%v,,0", NOSEL},
    {"RELEASE", KT_LOG, 0, 8000, 50, "i%iv0B,1,,,%v,0", NOSEL},
    {"ATTACK", KT_LIN, 0, 1000, 0, "i%iv0A%v,1,,,,0", NOSEL},         /* ADSR */
    {"DECAY", KT_LOG, 0, 2000, 50, "i%iv0A,1,%v,,,0", NOSEL},
    {"SUSTAIN", KT_LIN, 0, 1, 0, "i%iv0A,1,,%v,,0", NOSEL},
    {"RELEASE", KT_LOG, 0, 8000, 50, "i%iv0A,1,,,%v,0", NOSEL},
    {"LOW", KT_LIN, -15, 15, 0, "i%ix%v", NOSEL},                     /* EQ */
    {"MID", KT_LIN, -15, 15, 0, "i%ix,%v", NOSEL},
    {"HIGH", KT_LIN, -15, 15, 0, "i%ix,,%v", NOSEL},
    {"LEVEL", KT_LOG, 0, 1, 0.1f, "i%ik%v", NOSEL},                   /* CHORUS */
    {"FREQ", KT_LOG, 0.1f, 20, 0, "i%ik,,%v", NOSEL},
    {"DEPTH", KT_LIN, 0.01f, 1, 0, "i%ik,,,%v", NOSEL},
    {"LEVEL", KT_LOG, 0, 1, 0.1f, "i%ih%v", NOSEL},                   /* REVERB */
    {"LIVE", KT_LIN, 0, 1, 0, "i%ih,%v", NOSEL},
    {"DAMP", KT_LIN, 0, 1, 0, "i%ih,,%v", NOSEL},
    {"LEVEL", KT_LIN, 0, 2, 0, "i%iM%v", NOSEL},                      /* ECHO */
    {"DELAY", KT_LIN, 0, 5000, 0, "i%iM,%v", NOSEL},
    {"FEEDBACK", KT_LIN, 0, 1, 0, "i%iM,,,%v", NOSEL},
    {"TYPE", KT_SEL, 0, 0, 0, "i%i%v", SEL(DIST, DIST_V)},    /* DISTORTION */
    {"DRIVE", KT_LOG, 0.0625f, 16, 0, "i%iGD%v", NOSEL},
    {"MIX", KT_LIN, 0, 1, 0, "i%iGM%v", NOSEL},
};
#define KN_N ((int)(sizeof KN / sizeof KN[0]))
_Static_assert(sizeof KN / sizeof KN[0] == AMY_JUNO_NV, "tools/gen_amy_juno.py writes one value per knob");

typedef struct {
    const char *name;
    uint8_t first, n, patch;           /* patch: a patch change sets these knobs */
} page_t;
static const page_t PG[] = {
    {"OSC A", 0, 4, 1}, {"OSC B", 4, 4, 1}, {"LFO", 8, 4, 1}, {"VCF", 12, 4, 1}, {"VCF ENV", 16, 4, 1},
    {"ADSR", 20, 4, 1}, {"EQ", 24, 3, 1}, {"CHORUS", 27, 3, 1}, {"REVERB", 30, 3, 0}, {"ECHO", 33, 3, 0},
    {"DISTORTION", 36, 3, 0},
};
#define PG_N ((int)(sizeof PG / sizeof PG[0]))
#define PG_ECHO 9                      /* shown, not played (see the top) */
#define PRESET_REST_MS 150u

static struct {
    int patch, page, octave, load;     /* load: the preset still to load (PRESET is turning) */
    uint32_t load_t;                   /* when PRESET last turned */
    float val[KN_N];                   /* a continuous knob's value, a selector's option */
    uint32_t btn_was;
    uint8_t dirty;                     /* redraw: 1 header, 2 page names, 4 knobs */
} am;

static float kn_step(int k, int step) /* a continuous knob's value at step 0..127 */
{
    const knob_t *c = &KN[k];
    float t = (float)step / 127.0f;
    if (c->type == KT_LOG)
        return (c->lo + c->off) * powf((c->hi + c->off) / (c->lo + c->off), t) - c->off;
    return c->lo + (c->hi - c->lo) * t;
}

static uint8_t kn_pos(int k, float v)  /* the step (or option) nearest v */
{
    const knob_t *c = &KN[k];
    float t;
    int i;
    if (c->type == KT_SEL) {           /* a wave number; the distortion type is its index */
        for (i = 0; i < c->nopt; i++)
            if (c->optv == WAVE_V ? (float)WAVE_NUM[i] == v : (float)i == v)
                return (uint8_t)i;
        return 0;
    }
    if (c->type == KT_LOG)
        t = v + c->off <= c->lo + c->off ? 0.0f : logf((v + c->off) / (c->lo + c->off)) / logf((c->hi + c->off) / (c->lo + c->off));
    else
        t = (v - c->lo) / (c->hi - c->lo);
    t = t * 127.0f + 0.5f;
    return (uint8_t)(t < 0.0f ? 0.0f : t > 127.0f ? 127.0f : t);
}

/* a float for a wire message, without printf's floats (soft doubles on the device) */
static char *fnum(char *b, float x)
{
    uint32_t ip, fp;
    const char *sg = x < 0.0f ? "-" : "";
    if (x < 0.0f)
        x = -x;
    ip = (uint32_t)x;
    fp = (uint32_t)((x - (float)ip) * 10000.0f + 0.5f);
    if (fp >= 10000u) {
        ip++;
        fp -= 10000u;
    }
    sprintf(b, "%s%lu.%04lu", sg, (unsigned long)ip, (unsigned long)fp);
    return b;
}

/* a value as the editor shows it: three significant digits (its formatValue) */
static char *fmt3(char *b, float v)
{
    float a = v < 0.0f ? -v : v;
    int dec = a < 10.0f ? 2 : a < 100.0f ? 1 : 0;
    uint32_t scale = dec == 2 ? 100u : dec == 1 ? 10u : 1u, s = (uint32_t)(a * (float)scale + 0.5f);
    if (dec)
        sprintf(b, "%s%lu.%0*lu", v < 0.0f && s ? "-" : "", (unsigned long)(s / scale), dec, (unsigned long)(s % scale));
    else
        sprintf(b, "%s%lu", v < 0.0f && s ? "-" : "", (unsigned long)s);
    return b;
}

static void kn_send(int k)             /* knob k's value to AMY: its change_code, %i 1, %v the value */
{
    const knob_t *c = &KN[k];
    char m[96], v[16];
    const char *s = c->code, *val = c->type == KT_SEL ? c->optv[(int)am.val[k]] : fnum(v, am.val[k]);
    uint32_t n = 0;
    for (; *s && n < sizeof m - 16u; s++) {
        if (s[0] == '%' && s[1] == 'i') {
            m[n++] = '1';
            s++;
        } else if (s[0] == '%' && s[1] == 'v') {
            const char *t = val;
            while (*t)
                m[n++] = *t++;
            s++;
        } else {
            m[n++] = *s;
        }
    }
    m[n] = 0;
    engine_amy_send(m);
}

static void amy_patch(int n)           /* show preset n: it loads when PRESET has rested (amy_ui_frame) */
{
    int p, k;
    am.patch = (n % AMY_NJUNO + AMY_NJUNO) % AMY_NJUNO;
    am.load = 1;
    am.load_t = plat_ms();
    for (p = 0; p < PG_N; p++)
        for (k = PG[p].first; PG[p].patch && k < PG[p].first + PG[p].n; k++)
            am.val[k] = KN[k].type == KT_SEL ? (float)kn_pos(k, AMY_JUNO[am.patch].v[k]) : AMY_JUNO[am.patch].v[k];
    am.dirty |= 1u | 4u;
}

/* ---- the screen: header 0..72, page names 72..96, knobs 96..240 ---- */
static void amy_draw(void)
{
    char b[48];
    const char *name = AMY_JUNO[am.patch].name, *sp = strchr(name, ' ');
    const page_t *pg = &PG[am.page];
    int k, x, off = am.page == PG_ECHO;
    if (am.dirty & 1u) {
        cv_begin(240, 72, C_BLACK);
        cv_text(8, 6, &FONT_T, "AMY  JUNO-60", C_GRAY);
        if (am.octave) {
            sprintf(b, "OCT %+d", am.octave);
            text_r(232, 6, &FONT_T, b, C_AMB);
        }
        memcpy(b, name, (size_t)(sp ? sp - name : 0));   /* the bank-and-number, A11 .. B88 */
        b[sp ? sp - name : 0] = 0;
        x = cv_text(8, 22, &FONT_M, b, C_WHITE);
        cv_text(x + 8, 30, &FONT_S, sp ? sp + 1 : name, C_HI);
        sprintf(b, "%d / %d", am.patch + 1, AMY_NJUNO);
        cv_text(8, 52, &FONT_T, b, C_DIM);
        cv_rect(0, 71, 240, 1, C_LINE);
        cv_blit(0, 0);
    }
    if (am.dirty & 2u) {               /* the page, with its neighbours (SELECT's ring) either side */
        cv_begin(240, 24, C_BLACK);
        cv_text(8, 7, &FONT_T, PG[(am.page + PG_N - 1) % PG_N].name, C_DIM);
        text_r(232, 7, &FONT_T, PG[(am.page + 1) % PG_N].name, C_DIM);
        text_c(120, 4, &FONT_B, pg->name, C_WHITE);
        cv_rect(120 - text_w(&FONT_B, pg->name) / 2, 21, text_w(&FONT_B, pg->name), 2, C_HI);
        cv_blit(0, 72);
    }
    if (am.dirty & 4u) {
        cv_begin(240, 144, C_BLACK);
        for (k = 0; k < pg->n; k++) {
            int i = pg->first + k, y = 36 * k + 4;
            const knob_t *c = &KN[i];
            sprintf(b, "K%d", k + 1);
            cv_text(8, y + 1, &FONT_T, b, C_DIM);
            cv_text(30, y, &FONT_XS, c->name, off ? C_DIM : C_GRAY);
            text_r(232, y, &FONT_S, c->type == KT_SEL ? c->opt[(int)am.val[i]] : fmt3(b, am.val[i]), off ? C_DIM : C_HI);
            cv_rect(30, y + 20, 202, 5, C_LINE);
            cv_rect(30, y + 20, c->type == KT_SEL ? 202 * (int)am.val[i] / (c->nopt - 1) : 202 * kn_pos(i, am.val[i]) / 127, 5,
                    off ? C_DIM : C_HI);
        }
        if (off)
            text_c(120, 36 * 3 + 12, &FONT_XS, "NOT ON THE FM-1 (NO ROOM)", C_AMB);
        cv_blit(0, 96);
    }
    am.dirty = 0;
}

static void amy_ui_init(void)
{
    int k;
    palette_set(proj.set.palette);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    am.page = 0;
    am.octave = 0;
    am.btn_was = 0;
    for (k = 0; k < KN_N; k++)         /* every knob, the bus effects too, at AMY's power-on value */
        am.val[k] = KN[k].type == KT_SEL ? (float)kn_pos(k, AMY_JUNO[X0X_AMY_PATCH].v[k]) : AMY_JUNO[X0X_AMY_PATCH].v[k];
    amy_patch(X0X_AMY_PATCH);
    am.load = 0;                       /* power-on: now, not after a rest */
    engine_amy_preset(AMY_JUNO_BASE, AMY_JUNO[am.patch].msg, AMY_JUNO_NMSG);
    am.dirty = 7u;
}

static void amy_ui_frame(void)
{
    static const int KNOB[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
    const page_t *pg;
    uint32_t btn = plat_buttons(), down = btn & ~am.btn_was;
    int32_t d;
    int k;
    am.btn_was = btn;
    if ((d = plat_enc(EN_PRESET)) != 0)
        amy_patch(am.patch + (int)d);
    if (am.load && plat_ms() - am.load_t >= PRESET_REST_MS) {
        am.load = 0;
        engine_amy_preset(AMY_JUNO_BASE, AMY_JUNO[am.patch].msg, AMY_JUNO_NMSG);
    }
    if ((d = plat_enc(EN_SELECT)) != 0) {
        am.page = ((am.page + (int)d) % PG_N + PG_N) % PG_N;
        am.dirty |= 2u | 4u;
    }
    pg = &PG[am.page];
    for (k = 0; k < 4; k++)
        if ((d = plat_enc(KNOB[k])) != 0 && k < pg->n && am.page != PG_ECHO) {
            int i = pg->first + k, sel = KN[i].type == KT_SEL, top = sel ? KN[i].nopt - 1 : 127;
            int v = (sel ? (int)am.val[i] : kn_pos(i, am.val[i])) + (int)d;
            float nv;
            v = v < 0 ? 0 : v > top ? top : v;
            nv = sel ? (float)v : kn_step(i, v);
            if (nv != am.val[i]) {
                am.val[i] = nv;
                kn_send(i);
                am.dirty |= 4u;
            }
        }
    if (down & ((1u << B_OCTDN) | (1u << B_OCTUP))) {
        am.octave += (down & (1u << B_OCTUP)) ? 1 : -1;
        am.octave = am.octave < -2 ? -2 : am.octave > 2 ? 2 : am.octave;
        engine_amy_octave(am.octave);
        am.dirty |= 1u;
    }
    if (am.dirty)
        amy_draw();
    plat_leds((am.octave < 0 ? 1u << B_OCTDN : 0u) | (am.octave > 0 ? 1u << B_OCTUP : 0u), plat_keys());
}
