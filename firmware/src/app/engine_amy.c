/* SPDX-License-Identifier: GPL-3.0-only */
/* X0X_AMY: the AMY synthesizer (github.com/shorepine/amy) in place of X0X's engines.
 *
 * engine_render() hands each block to engine_amy_render(), in the audio ISR. AMY is only ever
 * touched from that context: the keys are read there (plat_keys) and their edges become AMY
 * messages, and the UI's messages (engine_amy_send, main loop) wait in a queue the ISR empties
 * before the block renders, so the main loop never races the render.
 *
 * The 27 keys play AMY synth 1 (a Juno-60 patch from ui_amy.c, X0X_AMY_PATCH at power-on), and so
 * does USB MIDI on channel 1; the AMYboard editor (amyboard.com/editor, "control" mode) reaches AMY
 * over USB SysEx (see "MIDI" below). */
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
#ifndef X0X_AMY_KEYS_OUT
#define X0X_AMY_KEYS_OUT 0             /* 1: the keys also go out over USB MIDI, channel 1 (off: the
                                        * editor echoes MIDI back to the board, which would double them) */
#endif
/* a ring's writer publishes its data before its index: a compiler barrier (the cores' order is in-order) */
#define PUBLISH() __atomic_signal_fence(__ATOMIC_SEQ_CST)

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
    PUBLISH();
    mq_w = w + 1u;
    return 0;
}

static volatile int8_t octave;         /* the keys' octave shift (OCT- / OCT+) */
void engine_amy_octave(int o) { octave = (int8_t)o; }

static uint32_t keys_was;
static uint8_t key_note[NKEYS];        /* the note each held key started: its release ends that one */
static uint8_t sounding;               /* the last block was not silent */

int engine_amy_sounding(void) { return sounding; }

/* ---- MIDI over USB ----
 * In: channel messages (USB-MIDI packets, plat_midi_in) go to AMY's own MIDI handling, so channel 1
 *   plays synth 1 and its controllers reach AMY's MIDI mappings. SysEx under SPSS's id 00 03 45
 *   carries AMY wire messages: the AMYboard editor's protocol. usb.c hands every SysEx byte to
 *   engine_amy_sysex_byte (TIMER5 ISR); whole frames wait in a ring that the audio ISR empties.
 * Each frame is answered AK once handled (the editor sends its next message only then), a zI ping OK.
 * The editor's AMYboard-only commands are answered and otherwise ignored: zP (Python), zB (reboot),
 *   zD (read a file), zF / zS / zO (samples). Its uploads (zT, a file; z<preset>, a sample) are
 *   followed by base64 chunks, which are counted off and dropped rather than read as AMY messages.
 * Out: AMY's MIDI output (its external output hook), and the replies, as USB-MIDI packets. */
#define SX_SLOTS 4u
#define SX_LEN 512u                    /* a frame's payload: longer ones are answered, not played */
static char sx_ring[SX_SLOTS][SX_LEN];
static uint16_t sx_len[SX_SLOTS];      /* SX_LEN: it overflowed */
static volatile uint32_t sx_w, sx_r;
static uint32_t sx_pos;
static uint8_t sx_state;               /* 0 none, 1..3 matching 00 03 45, 4 collecting, 5 not ours */
static uint32_t sx_skip;               /* an upload's base64 characters still to come (dropped) */

void engine_amy_sysex_byte(uint8_t b)  /* usb.c (TIMER5 ISR) / the host: each SysEx byte, F0 .. F7 */
{
    static const uint8_t ID[3] = {0x00, 0x03, 0x45};
    if (b == 0xF0) {
        sx_state = sx_w - sx_r < SX_SLOTS ? 1u : 5u;   /* ring full: dropped (the sender times out) */
        sx_pos = 0;
    } else if (b == 0xF7) {
        if (sx_state == 4u) {
            uint32_t k = sx_w % SX_SLOTS;
            sx_len[k] = (uint16_t)(sx_pos < SX_LEN ? sx_pos : SX_LEN);
            if (sx_pos < SX_LEN)
                sx_ring[k][sx_pos] = 0;
            PUBLISH();
            sx_w++;
        }
        sx_state = 0;
    } else if (sx_state >= 1u && sx_state <= 3u) {
        sx_state = b == ID[sx_state - 1u] ? (uint8_t)(sx_state + 1u) : 5u;
    } else if (sx_state == 4u) {
        if (sx_pos < SX_LEN - 1u)
            sx_ring[sx_w % SX_SLOTS][sx_pos] = (char)b;
        sx_pos++;
    }
}

/* MIDI bytes (one message, or one whole SysEx) as USB-MIDI packets, cable 0; the audio ISR only */
static void midi_send(uint8_t *b, uint16_t n)
{
    uint32_t i = 0;
    if (!n)
        return;
    if (b[0] == 0xF0) {                /* SysEx: CIN 4 for three bytes that go on, 5 / 6 / 7 to end */
        while (i < n) {
            uint32_t k = n - i > 3u || (n - i == 3u && b[n - 1u] != 0xF7) ? 3u : n - i;
            uint32_t cin = k == 3u && (n - i > 3u || b[n - 1u] != 0xF7) ? 4u : 4u + k;
            plat_midi_out(cin | (uint32_t)b[i] << 8 | (k > 1u ? (uint32_t)b[i + 1u] << 16 : 0u) |
                          (k > 2u ? (uint32_t)b[i + 2u] << 24 : 0u));
            i += k;
        }
    } else if (b[0] >= 0x80 && b[0] < 0xF0) {
        plat_midi_out((uint32_t)(b[0] >> 4) | (uint32_t)b[0] << 8 | (n > 1u ? (uint32_t)b[1] << 16 : 0u) |
                      (n > 2u ? (uint32_t)b[2] << 24 : 0u));
    } else if (b[0] >= 0xF8) {         /* real time: clock, start, stop */
        plat_midi_out(0x0Fu | (uint32_t)b[0] << 8);
    }
}

static void spss_reply(char a, char c)
{
    uint8_t f[7] = {0xF0, 0x00, 0x03, 0x45, (uint8_t)a, (uint8_t)c, 0xF7};
    midi_send(f, sizeof f);
}

/* the number after the nth comma: an upload's size */
static uint32_t field_after_comma(const char *m, int nth)
{
    uint32_t v = 0;
    for (; *m && nth; m++)
        nth -= *m == ',';
    for (; *m >= '0' && *m <= '9'; m++)
        v = v * 10u + (uint32_t)(*m - '0');
    return v;
}

static void spss_frame(char *m, uint32_t n)   /* the audio ISR: one 00 03 45 frame's payload */
{
    if (n >= SX_LEN) {
        /* too long for the ring: answered, not played */
    } else if (m[0] == 'z' && m[1] == 'I') {
        spss_reply('O', 'K');          /* ping: OK, not AK (as AMY's own parse_sysex) */
        return;
    } else if (sx_skip) {
        sx_skip = n >= sx_skip ? 0u : sx_skip - n;
    } else if (m[0] == 'z' && m[1] == 'T') {
        sx_skip = (field_after_comma(m, 1) + 2u) / 3u * 4u;   /* zT<file>,<bytes>: its base64 */
    } else if (m[0] == 'z' && m[1] >= '0' && m[1] <= '9') {
        sx_skip = (field_after_comma(m, 1) * 2u + 2u) / 3u * 4u;   /* z<preset>,<frames>,...: 16-bit */
    } else if (m[0] == 'z' && m[1] && strchr("PBDFSO", m[1])) {
        /* the AMYboard's own: nothing to do here */
    } else {
        amy_add_message(m);
    }
    spss_reply('A', 'K');
}

static void midi_in(void)              /* the audio ISR: what came in over USB since the last block */
{
    uint32_t pkt, guard = 64u;
    while (sx_r != sx_w) {
        uint32_t k = sx_r % SX_SLOTS;
        PUBLISH();
        spss_frame(sx_ring[k], sx_len[k]);
        sx_r++;
    }
    while (guard-- && plat_midi_in(&pkt)) {
        uint32_t cin = pkt & 15u;
        uint8_t b[3] = {(uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24)};
        if (cin >= 8u && cin <= 0xEu)  /* channel messages; real time is not used */
            amy_event_midi_message_received(b, cin == 0xCu || cin == 0xDu ? 2u : 3u, 0);
    }
}

void engine_amy_init(void)
{
    char m[32];
    amy_config_t c = amy_default_config();
    c.features.reverb = 1;             /* its delay lines (~138 KB) only when its level goes up */
    c.features.echo = 0;               /* ~98..295 KB: no room for it beside the reverb (ui_amy.c) */
    c.features.chorus = 1;             /* most Juno patches switch it on */
    c.features.partials = c.features.custom = c.features.default_synths = 0;
    c.platform.multicore = c.platform.multithread = 0;
    c.max_oscs = AMY_MAX_OSCS;
    c.max_voices = 16;
    c.max_synths = 8;
    c.max_memory_patches = 4;
    c.max_sequencer_tags = 32;
    c.amy_external_midi_output_hook = midi_send;
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
#if X0X_AMY_KEYS_OUT
            {
                uint8_t on[3] = {(uint8_t)((k >> i) & 1u ? 0x90 : 0x80), key_note[i], 100};
                midi_send(on, 3);
            }
#endif
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
    midi_in();
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
