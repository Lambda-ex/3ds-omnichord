#include "audio.h"
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------------------
 * Voice-pool synthesis.
 *
 *   - Audio runs as a continuous double-buffered ndsp stream.
 *   - Each "voice" is one sine oscillator with sample-accurate scheduling
 *     and an exponential decay envelope.
 *   - Sustained voices (R-hold) skip the decay step until released.
 *   - Per-chunk dynamic gain compresses output when many voices stack.
 *   - Saturator is unbounded-input soft sigmoid (x / sqrt(1+x^2)).
 *   - Vibrato modulates sustained voices' frequency only.
 * ------------------------------------------------------------------------- */

#define SAMPLE_RATE     22050
#define CHUNK_SAMPLES   1024
#define CHUNK_SIZE      (CHUNK_SAMPLES * 2 * (int)sizeof(s16))
#define NUM_BUFS        2
#define NUM_VOICES      32
#define STRUM_STRIDE_MS 28
#define DECAY_SECONDS   1.6f
#define HOLD_RELEASE_S  0.5f
#define VIBRATO_RATE_HZ 5.5f
#define VIBRATO_DEPTH   0.028f          /* max +/- pitch wobble */

#define INTERVAL_END -1
static const int INTERVALS[NUM_VARIANTS][5] = {
    { 0, 4, 7, INTERVAL_END, INTERVAL_END },   /* maj */
    { 0, 3, 7, INTERVAL_END, INTERVAL_END },   /* min */
    { 0, 4, 7, 10,           INTERVAL_END },   /* 7   */
    { 0, 5, 7, INTERVAL_END, INTERVAL_END },   /* sus */
    { 0, 3, 6, INTERVAL_END, INTERVAL_END },   /* dim */
};

static const char *VAR_SUFFIX[NUM_VARIANTS] = { "", "m", "7", "sus", "dim" };

static const float BASE_FREQ[NUM_NOTES] = {
    261.63f, 277.18f, 293.66f, 311.13f, 329.63f, 349.23f,
    369.99f, 392.00f, 415.30f, 440.00f, 466.16f, 493.88f,
};

#define PAT_END -1
static const int PATTERNS[NUM_PATTERNS][14] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, PAT_END },
    { 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, PAT_END },
    { 0, 2, 4, 6, 8, 10, PAT_END },
    { 0, 3, 6, 9, PAT_END },
    { 11, 9, 7, 5, 3, 1, PAT_END },
    { 11, 8, 5, 2, PAT_END },
};

typedef struct {
    bool   active;
    bool   sustained;
    float  freq;
    float  phase;
    float  freq_inc;       /* cached: 2pi * freq / SR */
    u64    start_sample;
    float  amplitude;
    float  decay;
} Voice;

static Voice voices[NUM_VOICES];

static s16        *bufs[NUM_BUFS];
static ndspWaveBuf waveBufs[NUM_BUFS];
static u64         global_sample = 0;

static int  sel_note    = 0;
static int  sel_variant = VAR_MAJ;
static int  octave_off  = 0;
static int  last_slot   = -1;
static bool hold_active = false;

/* Mixer state */
static float gain_current   = 0.9f;
static float vib_target     = 0.0f;
static float vib_current    = 0.0f;
static float vib_phase      = 0.0f;

/* --- helpers ------------------------------------------------------------ */

int audio_chord_size(int variant) {
    int n = 0;
    for (int i = 0; i < 5; i++) {
        if (INTERVALS[variant][i] == INTERVAL_END) break;
        n++;
    }
    return n;
}

int audio_chord_semi(int variant, int i) {
    return INTERVALS[variant][i];
}

const char *audio_variant_suffix(int variant) {
    return VAR_SUFFIX[variant];
}

float audio_strum_freq(int strum_idx) {
    int sz = audio_chord_size(sel_variant);
    if (sz < 1) sz = 1;
    int octave = strum_idx / sz;
    int tone   = strum_idx % sz;
    int semi   = INTERVALS[sel_variant][tone];
    float exp_ = (float)semi / 12.0f + (float)(octave - 1 + octave_off);
    return BASE_FREQ[sel_note] * powf(2.0f, exp_);
}

int audio_last_strum_slot(void) {
    return last_slot;
}

/* --- voice management --------------------------------------------------- */

static float decay_per_sample(float seconds) {
    return powf(0.001f, 1.0f / (seconds * (float)SAMPLE_RATE));
}

static int alloc_voice_slot(void) {
    for (int i = 0; i < NUM_VOICES; i++)
        if (!voices[i].active) return i;
    int pick = -1;
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voices[i].sustained) continue;
        if (pick < 0 || voices[i].amplitude < voices[pick].amplitude) pick = i;
    }
    if (pick < 0) pick = 0;
    return pick;
}

static void put_voice(int slot, float freq, u64 delay_samples,
                      float vol, bool sustained) {
    voices[slot].active       = true;
    voices[slot].sustained    = sustained;
    voices[slot].freq         = freq;
    voices[slot].phase        = 0.0f;
    voices[slot].freq_inc     = 2.0f * (float)M_PI * freq / (float)SAMPLE_RATE;
    voices[slot].start_sample = global_sample + delay_samples;
    voices[slot].amplitude    = vol;
    voices[slot].decay        = decay_per_sample(DECAY_SECONDS);
}

static void silence_sustained_internal(void) {
    for (int i = 0; i < NUM_VOICES; i++)
        if (voices[i].active && voices[i].sustained) voices[i].active = false;
}

/* Smooth, unbounded-input soft sigmoid. */
static inline float soft_clip(float x) {
    return x / sqrtf(1.0f + x * x);
}

static void fillChunk(s16 *buf) {
    const float twopi      = 2.0f * (float)M_PI;
    const float vib_phase_inc = twopi * VIBRATO_RATE_HZ / (float)SAMPLE_RATE;

    /* Per-chunk dynamic gain: tame the sum when many voices stack. */
    int active_n = 0;
    for (int v = 0; v < NUM_VOICES; v++) if (voices[v].active) active_n++;
    float gain_target = 1.0f / (1.0f + 0.10f * (float)active_n);

    for (int s = 0; s < CHUNK_SAMPLES; s++) {
        u64 sample_idx = global_sample + (u64)s;

        /* Glide gain + vibrato */
        gain_current += (gain_target - gain_current) * 0.0008f;
        vib_current  += (vib_target  - vib_current)  * 0.0005f;
        vib_phase    += vib_phase_inc;
        if (vib_phase >= twopi) vib_phase -= twopi;
        float vib_mult = 1.0f + vib_current * VIBRATO_DEPTH * sinf(vib_phase);

        float mix = 0.0f;
        for (int v = 0; v < NUM_VOICES; v++) {
            if (!voices[v].active) continue;
            if (sample_idx < voices[v].start_sample) continue;

            mix += sinf(voices[v].phase) * voices[v].amplitude;

            /* Vibrato applies only to sustained voices. */
            float inc = voices[v].sustained
                        ? voices[v].freq_inc * vib_mult
                        : voices[v].freq_inc;
            voices[v].phase += inc;
            if (voices[v].phase >= twopi) voices[v].phase -= twopi;

            if (!voices[v].sustained) {
                voices[v].amplitude *= voices[v].decay;
                if (voices[v].amplitude < 0.0005f) voices[v].active = false;
            }
        }

        float clipped = soft_clip(mix * gain_current);
        float fv      = clipped * 31000.0f;
        s16   iv      = (s16)fv;
        buf[s * 2 + 0] = iv;
        buf[s * 2 + 1] = iv;
    }
    global_sample += CHUNK_SAMPLES;
    DSP_FlushDataCache(buf, CHUNK_SIZE);
}

/* --- public API --------------------------------------------------------- */

void audio_init(void) {
    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspSetMasterVol(1.0f);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(0, mix);

    memset(voices, 0, sizeof(voices));
    gain_current = 0.9f;
    vib_target   = 0.0f;
    vib_current  = 0.0f;
    vib_phase    = 0.0f;

    for (int i = 0; i < NUM_BUFS; i++) {
        bufs[i] = (s16 *)linearAlloc(CHUNK_SIZE);
        memset(bufs[i], 0, CHUNK_SIZE);
        memset(&waveBufs[i], 0, sizeof(ndspWaveBuf));
        waveBufs[i].data_vaddr = bufs[i];
        waveBufs[i].nsamples   = CHUNK_SAMPLES;
        waveBufs[i].looping    = false;
        fillChunk(bufs[i]);
        waveBufs[i].status = NDSP_WBUF_FREE;
        ndspChnWaveBufAdd(0, &waveBufs[i]);
    }
}

void audio_exit(void) {
    ndspChnWaveBufClear(0);
    for (int i = 0; i < NUM_BUFS; i++)
        if (bufs[i]) linearFree(bufs[i]);
    ndspExit();
}

void audio_set_chord(int note, int variant) {
    if (note < 0 || note >= NUM_NOTES)          return;
    if (variant < 0 || variant >= NUM_VARIANTS) return;
    if (note == sel_note && variant == sel_variant) return;
    sel_note    = note;
    sel_variant = variant;
    if (hold_active) audio_chord_hold_on();
}

void audio_strum(int pattern) {
    if (pattern < 0 || pattern >= NUM_PATTERNS) pattern = PAT_UP;
    const int *pat = PATTERNS[pattern];
    const u64 stride = (u64)((SAMPLE_RATE * STRUM_STRIDE_MS) / 1000);
    last_slot = -1;
    for (int i = 0; i < 14; i++) {
        int slot = pat[i];
        if (slot == PAT_END) break;
        int s = alloc_voice_slot();
        put_voice(s, audio_strum_freq(slot), (u64)i * stride, 0.55f, false);
        last_slot = slot;
    }
}

void audio_pluck(int strum_idx) {
    if (strum_idx < 0 || strum_idx >= NUM_STRUM_NOTES) return;
    int s = alloc_voice_slot();
    put_voice(s, audio_strum_freq(strum_idx), 0, 0.55f, false);
    last_slot = strum_idx;
}

void audio_silence(void) {
    for (int i = 0; i < NUM_VOICES; i++) voices[i].active = false;
    last_slot   = -1;
    hold_active = false;
}

void audio_chord_hold_on(void) {
    silence_sustained_internal();

    /* One octave only — all chord tones in the same register. */
    int sz = audio_chord_size(sel_variant);
    for (int i = 0; i < sz; i++) {
        int semi  = audio_chord_semi(sel_variant, i);
        float exp_ = (float)semi / 12.0f + (float)octave_off;
        float freq = BASE_FREQ[sel_note] * powf(2.0f, exp_);
        int s = alloc_voice_slot();
        put_voice(s, freq, 0, 0.45f, true);
    }
    hold_active = true;
}

void audio_chord_hold_off(void) {
    float fast_decay = decay_per_sample(HOLD_RELEASE_S);
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voices[i].active && voices[i].sustained) {
            voices[i].sustained = false;
            voices[i].decay     = fast_decay;
        }
    }
    hold_active = false;
}

void audio_set_vibrato(float depth) {
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;
    vib_target = depth;
}

void audio_set_octave(int oct) {
    if (oct < -2) oct = -2;
    if (oct >  2) oct =  2;
    if (oct == octave_off) return;
    octave_off = oct;
    /* If a chord is sustaining, re-trigger at the new octave. */
    if (hold_active) audio_chord_hold_on();
}

void audio_update(void) {
    for (int i = 0; i < NUM_BUFS; i++) {
        if (waveBufs[i].status == NDSP_WBUF_DONE) {
            fillChunk(bufs[i]);
            waveBufs[i].status = NDSP_WBUF_FREE;
            ndspChnWaveBufAdd(0, &waveBufs[i]);
        }
    }
}
