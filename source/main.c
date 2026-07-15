#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <math.h>
#include "gui.h"
#include "audio.h"

#define CIRCLE_DEADZONE_SQ  (40 * 40)
#define CSTICK_DEADZONE      12

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *top    = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    gui_init();
    audio_init();

    /* Live preview — controls update these continuously */
    int  sel_note     = 0;
    int  sel_variant  = VAR_MAJ;
    int  sel_octave   = 0;

    /* Committed — what audio plays, updated only on L */
    int  played_note    = 0;
    int  played_variant = VAR_MAJ;
    int  played_octave  = 0;

    int  last_drag_slot = -1;
    bool dim_mode      = false;
    bool r_mode_lock   = false;       /* default: HOLD mode */
    bool hold_on       = false;

    audio_set_chord(played_note, played_variant);
    audio_set_octave(played_octave);

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        u32 kUp   = hidKeysUp();

        if (kDown & KEY_START) break;

        /* --- Circle pad -> live note preview --- */
        circlePosition cp;
        hidCircleRead(&cp);
        int mag2 = cp.dx * cp.dx + cp.dy * cp.dy;
        if (mag2 > CIRCLE_DEADZONE_SQ) {
            float ang   = atan2f((float)cp.dy, (float)cp.dx)
                          * 180.0f / (float)M_PI;
            float idx_f = (90.0f - ang) / 30.0f;
            int   idx   = (int)floorf(idx_f + 0.5f);
            idx = ((idx % 12) + 12) % 12;
            sel_note = idx;
        }

        /* --- Variant buttons (preview only) --- */
        if (kDown & KEY_X) sel_variant = VAR_MAJ;
        if (kDown & KEY_A) sel_variant = VAR_MIN;
        if (kDown & KEY_Y) sel_variant = VAR_7;
        if (kDown & KEY_B) sel_variant = dim_mode ? VAR_DIM : VAR_SUS;

        /* --- ZL: toggle sus<->dim switch. Flip current preview if on sus/dim --- */
        if (kDown & KEY_ZL) {
            dim_mode = !dim_mode;
            if (sel_variant == VAR_SUS)      sel_variant = VAR_DIM;
            else if (sel_variant == VAR_DIM) sel_variant = VAR_SUS;
        }

        /* --- D-pad -> preview octave (latched) --- */
        if (kDown & KEY_DRIGHT) sel_octave =  0;
        if (kDown & KEY_DUP)    sel_octave = +1;
        if (kDown & KEY_DDOWN)  sel_octave = -1;
        if (kDown & KEY_DLEFT)  sel_octave = +2;

        /* --- L: commit ALL three (note, variant, octave) --- */
        if (kDown & KEY_L) {
            played_note    = sel_note;
            played_variant = sel_variant;
            played_octave  = sel_octave;
            audio_set_chord(played_note, played_variant);
            audio_set_octave(played_octave);
        }

        /* --- ZR: toggle R-mode (HOLD <-> LOCK) --- */
        if (kDown & KEY_ZR) {
            r_mode_lock = !r_mode_lock;
            /* If switching from LOCK to HOLD while latched on but R isn't
             * currently held, clear the hold so the new mode is consistent. */
            if (!r_mode_lock && hold_on && !(kHeld & KEY_R)) {
                audio_chord_hold_off();
                hold_on = false;
            }
        }

        /* --- R: hold or lock the chord --- */
        if (r_mode_lock) {
            if (kDown & KEY_R) {
                if (hold_on) { audio_chord_hold_off(); hold_on = false; }
                else         { audio_chord_hold_on();  hold_on = true;  }
            }
        } else {
            if (kDown & KEY_R) { audio_chord_hold_on();  hold_on = true;  }
            if (kUp   & KEY_R) { audio_chord_hold_off(); hold_on = false; }
        }

        /* --- C-stick -> vibrato depth --- */
        circlePosition cs;
        hidCstickRead(&cs);
        float cmag  = sqrtf((float)(cs.dx * cs.dx + cs.dy * cs.dy));
        float vdepth = (cmag - (float)CSTICK_DEADZONE) / 140.0f;
        if (vdepth < 0.0f) vdepth = 0.0f;
        if (vdepth > 1.0f) vdepth = 1.0f;
        audio_set_vibrato(vdepth);

        /* --- Drag-strum on touchscreen (vertical bar, right half) --- */
        if (kHeld & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            int slot = gui_strum_slot_at(tp.px, tp.py);
            if (slot >= 0 && slot != last_drag_slot) {
                audio_pluck(slot);
                last_drag_slot = slot;
            } else if (slot < 0) {
                last_drag_slot = -1;
            }
        } else {
            last_drag_slot = -1;
        }

        audio_update();

        /* --- Render --- */
        GuiState st = {0};
        st.sel_note        = sel_note;
        st.sel_variant     = sel_variant;
        st.sel_octave      = sel_octave;
        st.played_note     = played_note;
        st.played_variant  = played_variant;
        st.played_octave   = played_octave;
        st.stick_x         = cp.dx;
        st.stick_y         = cp.dy;
        st.cstick_x        = cs.dx;
        st.cstick_y        = cs.dy;
        st.last_strum_slot = audio_last_strum_slot();
        st.hold_active     = hold_on;
        st.r_mode_lock     = r_mode_lock;
        st.dim_mode        = dim_mode;
        st.vibrato_depth   = vdepth;

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TargetClear(top, C2D_Color32(0x0d, 0x0d, 0x1a, 0xff));
        C2D_SceneBegin(top);
        gui_draw_top(&st);

        C2D_TargetClear(bottom, C2D_Color32(0x10, 0x10, 0x18, 0xff));
        C2D_SceneBegin(bottom);
        gui_draw_bottom(&st);

        C3D_FrameEnd(0);
    }

    audio_silence();
    audio_exit();
    gui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
