#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

void gui_init(void);
void gui_exit(void);

/* Per-frame state passed in from main.c */
typedef struct {
    /* Live preview — controlled by inputs continuously */
    int   sel_note;        /* 0..11                                  */
    int   sel_variant;     /* 0..NUM_VARIANTS-1                      */
    int   sel_octave;      /* -2..+2                                 */
    /* Committed — what audio plays; updated only on L */
    int   played_note;
    int   played_variant;
    int   played_octave;
    int   stick_x;
    int   stick_y;
    int   cstick_x;
    int   cstick_y;
    int   last_strum_slot; /* last touched slot for highlight, or -1 */
    bool  hold_active;     /* chord currently sustaining             */
    bool  r_mode_lock;     /* false=HOLD, true=LOCK                  */
    bool  dim_mode;        /* ZL switch: B = sus (false) or dim      */
    float vibrato_depth;
} GuiState;

void gui_draw_top(const GuiState *st);
void gui_draw_bottom(const GuiState *st);

/* Map bottom-screen touch x coord to a strum slot 0..12, or -1 if outside. */
int  gui_strum_slot_at(int x, int y);
