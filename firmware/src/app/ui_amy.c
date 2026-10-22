/* SPDX-License-Identifier: GPL-3.0-only */
/* X0X_AMY's UI (included by ui.c, which hands it ui_init / ui_frame): AMY's 128 Juno-60 patches
 * on the keys, and the patch's sound on the knobs.
 *
 *   PRESET   the patch (A11 .. B88)        SELECT   the page: VCF, ENV, OSC, LFO
 *   K1..K4   the page's four parameters    OCT- / OCT+   the keys' octave (-2 .. +2)
 *
 * A knob is 0..127 steps along its parameter's range (log for frequencies and times); choosing a
 * patch puts every knob where that patch has it (build/gen/amy_juno.h, tools/gen_amy_juno.py).
 * Each change goes to AMY as a wire message for synth 1 (engine_amy_send): a synth message naming
 * an osc reaches that osc in every voice. A Juno voice: osc 0 VCF / VCA, 1 LFO, 2 pulse, 3 saw,
 * 4 sub, 5 noise. Nothing is saved: power-on is X0X_AMY_PATCH with its own values. */
#include <stddef.h>
#include <stdio.h>
#ifdef X0X_HOST
#include <string.h>
#else
char *strchr(const char *s, int c);     /* newlib; its string.h clashes with libc.c's memcpy (unsigned n) */
#endif
#include <math.h>
#include "amy_juno.h"

enum { JP_CUTOFF, JP_RESO, JP_ENV, JP_LFO_VCF, JP_ATTACK, JP_DECAY, JP_SUSTAIN, JP_RELEASE,
       JP_PULSE, JP_SAW, JP_SUB, JP_NOISE, JP_LFO_RATE, JP_LFO_PITCH, JP_PW, JP_CHORUS, JP_N };
enum { CV_LIN, CV_LOG, CV_LOG0 };      /* LOG0: step 0 is 0, steps 1..127 logarithmic from lo */
enum { U_HZ, U_MS, U_PCT, U_NUM, U_SIGNED };
typedef struct {
    const char *name;
    uint8_t curve, unit;
    float lo, hi;
} jparam_t;
static const jparam_t JP[JP_N] = {
    {"CUTOFF", CV_LOG, U_HZ, 20.0f, 20000.0f},     {"RESONANCE", CV_LOG, U_NUM, 0.5f, 16.0f},
    {"ENV AMOUNT", CV_LIN, U_SIGNED, -12.0f, 12.0f}, {"LFO AMOUNT", CV_LIN, U_NUM, 0.0f, 2.0f},
    {"ATTACK", CV_LOG0, U_MS, 1.0f, 5000.0f},      {"DECAY", CV_LOG0, U_MS, 1.0f, 150000.0f},
    {"SUSTAIN", CV_LIN, U_PCT, 0.0f, 1.0f},        {"RELEASE", CV_LOG0, U_MS, 1.0f, 30000.0f},
    {"PULSE", CV_LIN, U_PCT, 0.0f, 1.0f},          {"SAW", CV_LIN, U_PCT, 0.0f, 1.0f},
    {"SUB", CV_LIN, U_PCT, 0.0f, 1.0f},            {"NOISE", CV_LIN, U_PCT, 0.0f, 1.0f},
    {"LFO RATE", CV_LOG, U_HZ, 0.1f, 30.0f},       {"LFO PITCH", CV_LIN, U_PCT, 0.0f, 0.05f},
    {"PULSE WIDTH", CV_LIN, U_PCT, 0.5f, 0.95f},   {"CHORUS", CV_LIN, U_PCT, 0.0f, 1.0f},
};
#define AMY_NPAGES 4
static const char *const AMY_PAGE[AMY_NPAGES] = {"VCF", "ENV", "OSC", "LFO"};
_Static_assert(AMY_JUNO_NV == JP_N, "tools/gen_amy_juno.py writes one value per JP_*");

static struct {
    int patch, page, octave;
    uint8_t pos[JP_N];                 /* each knob, 0..127 */
    uint32_t btn_was;
    uint8_t dirty;                     /* redraw: 1 header, 2 tabs, 4 knobs */
} am;

static float jp_value(int p)
{
    const jparam_t *j = &JP[p];
    float t = (float)am.pos[p] / 127.0f;
    if (j->curve == CV_LIN)
        return j->lo + (j->hi - j->lo) * t;
    if (j->curve == CV_LOG)
        return j->lo * powf(j->hi / j->lo, t);
    return am.pos[p] ? j->lo * powf(j->hi / j->lo, (float)(am.pos[p] - 1) / 126.0f) : 0.0f;
}

static uint8_t jp_pos(int p, float v)  /* the step nearest v */
{
    const jparam_t *j = &JP[p];
    float t;
    if (j->curve == CV_LIN)
        t = (v - j->lo) / (j->hi - j->lo) * 127.0f;
    else if (j->curve == CV_LOG)
        t = v <= j->lo ? 0.0f : logf(v / j->lo) / logf(j->hi / j->lo) * 127.0f;
    else
        t = v <= 0.0f ? 0.0f : v <= j->lo ? 1.0f : 1.0f + logf(v / j->lo) / logf(j->hi / j->lo) * 126.0f;
    t += 0.5f;
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

static void jp_send(int p)             /* knob p's value to AMY */
{
    char m[64], a[16], b[16], c[16], d[16];
    int o;
    switch (p) {
    case JP_CUTOFF: sprintf(m, "i1v0F%s", fnum(a, jp_value(p))); break;
    case JP_RESO: sprintf(m, "i1v0R%s", fnum(a, jp_value(p))); break;
    case JP_ENV: sprintf(m, "i1v0F,,,%s", fnum(a, jp_value(p))); break;        /* the eg0 coefficient */
    case JP_LFO_VCF: sprintf(m, "i1v0F,,,,,%s", fnum(a, jp_value(p))); break;  /* the mod coefficient */
    case JP_ATTACK: case JP_DECAY: case JP_SUSTAIN: case JP_RELEASE:           /* the ADSR, as bp0 */
        sprintf(m, "i1v0A%s,1,%s,%s,%s,0", fnum(a, jp_value(JP_ATTACK)), fnum(b, jp_value(JP_DECAY)),
                fnum(c, jp_value(JP_SUSTAIN)), fnum(d, jp_value(JP_RELEASE)));
        break;
    case JP_PULSE: case JP_SAW: case JP_SUB: case JP_NOISE:
        sprintf(m, "i1v%da%s", 2 + p - JP_PULSE, fnum(a, jp_value(p)));
        break;
    case JP_LFO_RATE: sprintf(m, "i1v1f%s", fnum(a, jp_value(p))); break;
    case JP_LFO_PITCH:                 /* the three pitched oscs' mod coefficient */
        for (o = 2; o <= 4; o++) {
            sprintf(m, "i1v%df,,,,,%s", o, fnum(a, jp_value(p)));
            engine_amy_send(m);
        }
        return;
    case JP_PW: sprintf(m, "i1v2d%s", fnum(a, jp_value(p))); break;
    case JP_CHORUS: sprintf(m, "k%s", fnum(a, jp_value(p))); break;
    default: return;
    }
    engine_amy_send(m);
}

static void amy_patch(int n)
{
    char m[24];
    int p;
    am.patch = (n % AMY_NJUNO + AMY_NJUNO) % AMY_NJUNO;
    sprintf(m, "i1iv%dK%d", X0X_AMY_VOICES, am.patch);
    engine_amy_send(m);
    for (p = 0; p < JP_N; p++)
        am.pos[p] = jp_pos(p, AMY_JUNO[am.patch].v[p]);
    am.dirty |= 1u | 4u;
}

/* ---- the screen: header 0..72, tabs 72..96, knobs 96..240 ---- */
static void jp_text(int p, char *b)
{
    float v = jp_value(p);
    int32_t i;
    switch (JP[p].unit) {
    case U_HZ:
        if (v >= 1000.0f)
            sprintf(b, "%d.%d KHZ", (int)(v / 1000.0f), (int)(v / 100.0f) % 10);
        else if (v >= 100.0f)
            sprintf(b, "%d HZ", (int)(v + 0.5f));
        else
            sprintf(b, "%d.%02d HZ", (int)v, (int)(v * 100.0f + 0.5f) % 100);
        break;
    case U_MS:
        if (v >= 1000.0f)
            sprintf(b, "%d.%d S", (int)(v / 1000.0f), (int)(v / 100.0f) % 10);
        else
            sprintf(b, "%d MS", (int)(v + 0.5f));
        break;
    case U_PCT:                        /* the value as a percentage, a decimal below 10 % */
        i = (int32_t)(v * 1000.0f + 0.5f);
        if (i < 100)
            sprintf(b, "%d.%d%%", (int)i / 10, (int)i % 10);
        else
            sprintf(b, "%d%%", (int)(i + 5) / 10);
        break;
    default:
        i = (int32_t)(v * 10.0f + (v < 0.0f ? -0.5f : 0.5f));
        sprintf(b, "%s%d.%d", JP[p].unit == U_SIGNED ? (i < 0 ? "-" : "+") : "", (int)(i < 0 ? -i : i) / 10,
                (int)(i < 0 ? -i : i) % 10);
        break;
    }
}

static void amy_draw(void)
{
    char b[48];
    const char *name = AMY_JUNO[am.patch].name, *sp = strchr(name, ' ');
    int k, x;
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
    if (am.dirty & 2u) {
        cv_begin(240, 24, C_BLACK);
        for (k = 0; k < AMY_NPAGES; k++) {
            text_c(30 + 60 * k, 5, &FONT_B, AMY_PAGE[k], k == am.page ? C_WHITE : C_DIM);
            if (k == am.page)
                cv_rect(10 + 60 * k, 21, 40, 2, C_HI);
        }
        cv_blit(0, 72);
    }
    if (am.dirty & 4u) {
        cv_begin(240, 144, C_BLACK);
        for (k = 0; k < 4; k++) {
            int p = am.page * 4 + k, y = 36 * k + 4;
            sprintf(b, "K%d", k + 1);
            cv_text(8, y + 1, &FONT_T, b, C_DIM);
            cv_text(30, y, &FONT_XS, JP[p].name, C_GRAY);
            jp_text(p, b);
            text_r(232, y, &FONT_S, b, C_HI);
            cv_rect(30, y + 20, 202, 5, C_LINE);
            cv_rect(30, y + 20, 202 * am.pos[p] / 127, 5, C_HI);
        }
        cv_blit(0, 96);
    }
    am.dirty = 0;
}

static void amy_ui_init(void)
{
    palette_set(proj.set.palette);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    am.page = 0;
    am.octave = 0;
    am.btn_was = 0;
    amy_patch(X0X_AMY_PATCH);          /* (engine_amy_init loaded it too: this sets the knobs) */
    am.dirty = 7u;
}

static void amy_ui_frame(void)
{
    static const int KNOB[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
    uint32_t btn = plat_buttons(), down = btn & ~am.btn_was;
    int32_t d;
    int k;
    am.btn_was = btn;
    if ((d = plat_enc(EN_PRESET)) != 0)
        amy_patch(am.patch + (int)d);
    if ((d = plat_enc(EN_SELECT)) != 0) {
        am.page = ((am.page + (int)d) % AMY_NPAGES + AMY_NPAGES) % AMY_NPAGES;
        am.dirty |= 2u | 4u;
    }
    for (k = 0; k < 4; k++)
        if ((d = plat_enc(KNOB[k])) != 0) {
            int p = am.page * 4 + k, v = am.pos[p] + (int)d;
            am.pos[p] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
            jp_send(p);
            am.dirty |= 4u;
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
