/* SPDX-License-Identifier: GPL-3.0-only */
/* X0X_AMY: the AMY synthesizer (github.com/shorepine/amy) in place of X0X's engines.
 *
 * engine_render() hands each block to engine_amy_render(), in the audio ISR. AMY is only ever
 * touched from that context: the keys are read there (plat_keys) and their edges become AMY
 * messages, and the UI's messages (engine_amy_send, main loop) wait in a queue the ISR empties
 * before the block renders, so the main loop never races the render.
 *
 * The 27 keys play AMY synth 1 (a Juno-60 patch from ui_amy.c, X0X_AMY_PATCH at power-on). */
#include <stdio.h>
#include <string.h>
#include "amy.h"
#include "engine.h"
#include "plat.h"

#ifndef X0X_AMY_VOLUME
#define X0X_AMY_VOLUME 2             /* AMY's global volume, 0..10 (its mix is 0.1 x this) */
#endif
#define AMY_MAX_OSCS 64                /* 6 voices: a Juno voice takes 6 oscs, a DX7 voice 8 */
#define KEY_NOTE0 53                   /* key 0 = F3, at octave 0 */

/* the platform layer AMY expects (i2s.c / libminiaudio-audio.c on its own platforms): AMY renders
 * only when engine_amy_render asks, and owns no audio device, MIDI port or thread here */
void amy_platform_init(void) {}
void amy_platform_deinit(void) {}
void amy_update_tasks(void) {}
int16_t *amy_render_audio(void) { return amy_simple_fill_buffer(); }
size_t amy_i2s_write(const uint8_t *buf, size_t n) { (void)buf; return n; }
void delay_ms(uint32_t ms) { (void)ms; }
void miniaudio_start(void) {}
void miniaudio_stop(void) {}
void run_midi(void) {}
void stop_midi(void) {}

/* main loop -> ISR: wire messages, single producer / single consumer */
#define MQ 32u
#define MQ_LEN 64u
static char mq[MQ][MQ_LEN];
static volatile uint32_t mq_w, mq_r;

int engine_amy_send(const char *m)
{
    uint32_t w = mq_w;
    if (w - mq_r >= MQ || strlen(m) >= MQ_LEN)
        return -1;
    strcpy(mq[w % MQ], m);
    mq_w = w + 1u;
    return 0;
}

static volatile int8_t octave;         /* the keys' octave shift (OCT- / OCT+) */
void engine_amy_octave(int o) { octave = (int8_t)o; }

static uint32_t keys_was;
static uint8_t key_note[NKEYS];        /* the note each held key started: its release ends that one */
static uint8_t sounding;               /* the last block was not silent */

int engine_amy_sounding(void) { return sounding; }

void engine_amy_init(void)
{
    char m[32];
    amy_config_t c = amy_default_config();
    c.features.reverb = c.features.echo = 0;
    c.features.chorus = 1;             /* most Juno patches switch it on */
    c.features.partials = c.features.custom = c.features.default_synths = 0;
    c.platform.multicore = c.platform.multithread = 0;
    c.max_oscs = AMY_MAX_OSCS;
    c.max_voices = 16;
    c.max_synths = 8;
    c.max_memory_patches = 4;
    c.max_sequencer_tags = 32;
    amy_start(c);
    snprintf(m, sizeof m, "i1iv%dK%d", X0X_AMY_VOICES, X0X_AMY_PATCH);
    amy_add_message(m);
    snprintf(m, sizeof m, "V%d", X0X_AMY_VOLUME);
    amy_add_message(m);
    keys_was = 0;
}

static void keys_to_notes(void)        /* ISR: each key that went down or up since the last block */
{
    uint32_t k = plat_keys(), ch = k ^ keys_was;
    char m[24];
    int i;
    for (i = 0; ch && i < NKEYS; i++, ch >>= 1)
        if (ch & 1u) {
            if ((k >> i) & 1u)
                key_note[i] = (uint8_t)(KEY_NOTE0 + 12 * octave + i);
            snprintf(m, sizeof m, "i1n%dl%d", key_note[i], (int)((k >> i) & 1u));
            amy_add_message(m);
        }
    keys_was = k;
}

/* ISR: n stereo frames (AMY's block) as engine_render's 24-bit int32. gain = master * the codec's
 * full scale: AMY's own clipper is the limiter, so its full scale is the codec's. */
void engine_amy_render(int32_t *out_lr, uint32_t n, float gain)
{
    const int16_t *s;
    float g = gain / 32768.0f;
    uint32_t i;
    int any = 0;
    if (n != AMY_BLOCK_SIZE) {         /* the device and the host both call with 256 */
        for (i = 0; i < 2u * n; i++)
            out_lr[i] = 0;
        return;
    }
    while (mq_r != mq_w) {
        amy_add_message(mq[mq_r % MQ]);
        mq_r++;
    }
    keys_to_notes();
    s = amy_simple_fill_buffer();
    for (i = 0; i < n; i++) {
        any |= s[2u * i] | s[2u * i + 1u];
        out_lr[2u * i] = (int32_t)((float)s[2u * i] * g);
        out_lr[2u * i + 1u] = (int32_t)((float)s[2u * i + 1u] * g);
        if (i & 1u)
            eng_scope[eng_scope_w++ & (SCOPE_N - 1u)] = (int16_t)(((int32_t)s[2u * i] + s[2u * i + 1u]) / 2);
    }
    sounding = any != 0;
}

/* ---- engine.h, in place of engine.c ----
 * X0X_AMY builds leave engine.c and the DSP units out (their code and the ~300 KB of pool buffers are
 * what make room for AMY on the device). The rest of the app still links against engine.h: the
 * project, the main loops and X0X's own UI. Here the sequencer is real (project.c and the host read
 * it) and the rest is inert: no part sounds, the knobs' sound_t is kept but drives nothing. */
seq_t seq;
volatile uint32_t eng_prof[ENG_PROF_N], eng_prof_frames;
volatile uint32_t eng_guard_count;
volatile uint8_t eng_step[NTRACKS];
volatile uint16_t eng_peak[NPARTS];
volatile uint8_t eng_hit[NKIT][NDRUM];
int16_t eng_scope[SCOPE_N];
volatile uint32_t eng_scope_w;

void engine_init(pattern_t *patterns, song_t *song, lane_t *lanes, const sound_t *base)
{
    (void)lanes;
    (void)base;
    seq_init(&seq, patterns);
    seq.song = song;
    engine_amy_init();
}

/* ISR: the codec's full scale is 2^23 (engine.c's NORMAL 2^22 x its 2.0 limit, LOUD 2^23 x 1.0) */
void engine_render(int32_t *out_lr, uint32_t n)
{
    eng_prof_frames += n;
    engine_amy_render(out_lr, n, (float)plat_master() / 4096.0f * 8388607.0f);
}

static const x0x_param_t no_param;     /* every value 0 (project.c reads a default or two) */
void engine_motion_enable(int on) { (void)on; }
void engine_motion_reset(void) {}
void engine_load(uint32_t pct, uint32_t frames) { (void)pct; (void)frames; }
void engine_guard_enable(int on) { (void)on; }
int engine_guard_active(void) { return 0; }
int engine_nvoices(int target) { (void)target; return 0; }
int engine_nparams(int target, int voice) { (void)target; (void)voice; return 0; }
const x0x_param_t *engine_param(int target, int voice, int i) { (void)target; (void)voice; (void)i; return &no_param; }
const char *engine_voice_name(int target, int voice) { (void)target; (void)voice; return ""; }
void engine_set(int target, int voice, int i, int value) { (void)target; (void)voice; (void)i; (void)value; }
void engine_out_loud(int on) { (void)on; }
void engine_usb_full(int on) { (void)on; }
void engine_volume(int32_t *out_lr, uint32_t n) { (void)out_lr; (void)n; }
void engine_apply_sound(const sound_t *s) { (void)s; }
void engine_sound_defaults(sound_t *s) { memset(s, 0, sizeof *s); }
void engine_sound_centre_drum_pans(sound_t *s) { (void)s; }
void engine_drum(int kit, int voice, float vel) { (void)kit; (void)voice; (void)vel; }
void engine_bass_on(int part, int note, int accent, int slide) { (void)part; (void)note; (void)accent; (void)slide; }
void engine_bass_off(int part) { (void)part; }
void engine_brk_live(int key, int down) { (void)key; (void)down; }
void engine_brk_loops(void) {}
int engine_brk_slice(void) { return -1; }
void engine_brk_state(int *slice, int *bank, int *div, int *running) { *slice = -1; *bank = *div = *running = 0; }
int engine_brk_outline(int which, uint8_t *peaks, int n) { (void)which; memset(peaks, 0, (unsigned)n); return 0; }
int engine_motion_part(int target, int voice) { (void)target; (void)voice; return 0; }
int engine_motion_value(int lane) { (void)lane; return -1; }
void engine_motion_rec(int lane) { (void)lane; }
void engine_motion_hold(int lane) { (void)lane; }
float engine_gr_db(void) { return 0.0f; }
void engine_master_format(int i, char *buf) { (void)i; buf[0] = 0; }
int engine_brk_nslots(void) { return 0; }
const char *const *engine_brk_slot_names(void) { static const char *const none[1] = {""}; return none; }
const x0x_param_t *breaks_param(int i) { (void)i; return &no_param; }   /* dsp/breaks.h: X0X's settings list */
