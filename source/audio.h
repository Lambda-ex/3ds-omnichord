#pragma once
#include <3ds.h>

/* Variant indices */
enum {
    VAR_MAJ = 0,
    VAR_MIN = 1,
    VAR_7   = 2,
    VAR_SUS = 3,
    VAR_DIM = 4,
    NUM_VARIANTS = 5,
};

/* Strum patterns (driven by C-stick direction). */
enum {
    PAT_UP        = 0,   /* full ascending  1..13 */
    PAT_DOWN      = 1,   /* full descending 13..1 */
    PAT_UP_R      = 2,   /* ascending odd indices */
    PAT_UP_L      = 3,   /* ascending wide gaps   */
    PAT_DOWN_R    = 4,
    PAT_DOWN_L    = 5,
    NUM_PATTERNS  = 6,
};

#define NUM_NOTES       12
#define NUM_STRUM_NOTES 12

void audio_init(void);
void audio_exit(void);

/* Set the currently-selected chord. Doesn't make sound until audio_strum
 * (or audio_pluck) is invoked. note in 0..11, variant in 0..NUM_VARIANTS-1. */
void audio_set_chord(int note, int variant);

/* Trigger a strum of the selected chord using the given pattern. */
void audio_strum(int pattern);

/* Play a single strum-strip note of the selected chord. idx in 0..12. */
void audio_pluck(int strum_idx);

/* Stop all currently-decaying voices immediately. */
void audio_silence(void);

/* Start a sustained chord at the currently-selected note/variant.
 * Notes ring continuously (no decay) until audio_chord_hold_off(). */
void audio_chord_hold_on(void);

/* Release a sustained chord — voices resume normal decay envelope. */
void audio_chord_hold_off(void);

/* Per-frame tick — keeps the audio buffers fed. */
void audio_update(void);

/* Vibrato depth 0..1. Applies only to sustained (hold) voices; strums
 * and plucks are unaffected. Internally smoothed so changes don't pop. */
void audio_set_vibrato(float depth);

/* Octave offset applied to all chord-derived frequencies. Range [-2..+2]. */
void audio_set_octave(int oct);

/* Helpers for the GUI. */
int   audio_chord_size(int variant);                 /* 3 or 4 */
int   audio_chord_semi(int variant, int i);          /* semitone offset */
const char *audio_variant_suffix(int variant);       /* "", "m", "7", "sus", "dim" */
float audio_strum_freq(int strum_idx);               /* pitch of strum slot */

/* Most-recently-strummed slot in 0..12 (for GUI highlight), or -1. */
int   audio_last_strum_slot(void);
