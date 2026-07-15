#include "gui.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- Layout: top screen 400x240 ---------------------------------------
 *   [   0.. 110]  Circle pad  (note)
 *   [ 112.. 218]  A/B/X/Y     (variant)
 *   [ 220.. 310]  D-pad       (octave)
 *   [ 312.. 400]  C-stick     (vibrato)
 *   Bottom strip (156..240): big chord name + breakdown
 * ---------------------------------------------------------------------- */

#define CP_PX    0
#define CP_PW    110
#define CP_CX    55
#define CP_CY    74
#define CP_R     42

#define BTN_PX   112
#define BTN_PW   106
#define BTN_CX   165
#define BTN_CY   74
#define BTN_R    28
#define BTN_RAD  20

#define DPAD_PX  220
#define DPAD_PW  90
#define DPAD_CX  265
#define DPAD_CY  74
#define DARM_W   20
#define DARM_H   24

#define CS_PX    312
#define CS_PW    88
#define CS_CX    356
#define CS_CY    74
#define CS_R     30

/* Bottom screen 320x240: vertical strum bar on the right half. */
#define STRUM_X       162
#define STRUM_W       154
#define STRUM_Y       2
#define STRUM_H       236
#define STRUM_SLOT_H  ((float)STRUM_H / NUM_STRUM_NOTES)

#define RGBA(r,g,b,a) ((u32)((r) | ((g) << 8) | ((b) << 16) | ((a) << 24)))

static const u32 NOTE_COLOR[12] = {
    RGBA(0xe7, 0x4c, 0x3c, 0xff),
    RGBA(0xe6, 0x7e, 0x22, 0xff),
    RGBA(0xf1, 0xc4, 0x0f, 0xff),
    RGBA(0xb7, 0xd3, 0x2a, 0xff),
    RGBA(0x27, 0xae, 0x60, 0xff),
    RGBA(0x16, 0xa0, 0x85, 0xff),
    RGBA(0x1a, 0xbc, 0x9c, 0xff),
    RGBA(0x29, 0x80, 0xb9, 0xff),
    RGBA(0x34, 0x49, 0x8d, 0xff),
    RGBA(0x8e, 0x44, 0xad, 0xff),
    RGBA(0xc0, 0x39, 0x7c, 0xff),
    RGBA(0xec, 0x70, 0xa8, 0xff),
};

static const u32 PANEL_DARK  = RGBA(0x1a, 0x1a, 0x2e, 0xff);
static const u32 GRID_DARK   = RGBA(0x22, 0x22, 0x32, 0xff);
static const u32 WHITE       = RGBA(0xff, 0xff, 0xff, 0xff);
static const u32 LIGHT_GREY  = RGBA(0xcc, 0xcc, 0xd5, 0xff);
static const u32 MID_GREY    = RGBA(0x55, 0x55, 0x66, 0xff);
static const u32 ACCENT_Y    = RGBA(0xff, 0xff, 0xaa, 0xff);
static const u32 HOLD_GLOW   = RGBA(0x4a, 0x80, 0xff, 0xff);
static const u32 LOCK_COL    = RGBA(0xff, 0x99, 0x66, 0xff);
static const u32 VIB_COL     = RGBA(0xff, 0x99, 0xcc, 0xff);
static const u32 PREVIEW_COL = RGBA(0xff, 0xd0, 0x77, 0xff);
static const u32 BOTTOM_BG   = RGBA(0x10, 0x10, 0x18, 0xff);

static C2D_TextBuf textBuf;
static C2D_Text noteText[NUM_NOTES];
static C2D_Text chordNameText[NUM_NOTES * NUM_VARIANTS];
static C2D_Text breakdownText[NUM_NOTES * NUM_VARIANTS];
static C2D_Text varLabel[NUM_VARIANTS];
static C2D_Text titleText;
static C2D_Text holdText, lockText, pressLText;
static C2D_Text vibText;
static C2D_Text octaveText[5];   /* "-2", "-1", "0", "+1", "+2" indexed -2..+2 -> 0..4 */

static const char *NOTE_NAMES[NUM_NOTES] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static void parse_into(C2D_Text *t, const char *s) {
    C2D_TextParse(t, textBuf, s);
    C2D_TextOptimize(t);
}

void gui_init(void) {
    textBuf = C2D_TextBufNew(4096);

    parse_into(&titleText,  "OMNICHORD");
    parse_into(&holdText,   "HOLD");
    parse_into(&lockText,   "LOCK");
    parse_into(&pressLText, "L = set");
    parse_into(&vibText,    "VIB");

    parse_into(&octaveText[0], "-2");
    parse_into(&octaveText[1], "-1");
    parse_into(&octaveText[2], "0");
    parse_into(&octaveText[3], "+1");
    parse_into(&octaveText[4], "+2");

    for (int n = 0; n < NUM_NOTES; n++)
        parse_into(&noteText[n], NOTE_NAMES[n]);

    parse_into(&varLabel[VAR_MAJ], "maj");
    parse_into(&varLabel[VAR_MIN], "min");
    parse_into(&varLabel[VAR_7],   "7");
    parse_into(&varLabel[VAR_SUS], "sus");
    parse_into(&varLabel[VAR_DIM], "dim");

    char buf[48];
    for (int n = 0; n < NUM_NOTES; n++) {
        for (int v = 0; v < NUM_VARIANTS; v++) {
            int idx = n * NUM_VARIANTS + v;
            snprintf(buf, sizeof(buf), "%s%s",
                     NOTE_NAMES[n], audio_variant_suffix(v));
            parse_into(&chordNameText[idx], buf);

            buf[0] = 0;
            int sz = audio_chord_size(v);
            for (int i = 0; i < sz; i++) {
                int note = (n + audio_chord_semi(v, i)) % 12;
                strcat(buf, NOTE_NAMES[note]);
                if (i < sz - 1) strcat(buf, " - ");
            }
            parse_into(&breakdownText[idx], buf);
        }
    }
}

void gui_exit(void) {
    C2D_TextBufDelete(textBuf);
}

/* ---- Helpers ---------------------------------------------------------- */

static void draw_text_centered(C2D_Text *t, float cx, float y, float scale,
                               u32 color) {
    float w, h;
    C2D_TextGetDimensions(t, scale, scale, &w, &h);
    C2D_DrawText(t, C2D_WithColor,
                 cx - w * 0.5f, y, 0.5f, scale, scale, color);
}

static u32 dim_color(u32 c, float factor) {
    int r = (c        & 0xff);
    int g = ((c >> 8) & 0xff);
    int b = ((c >> 16) & 0xff);
    int a = ((c >> 24) & 0xff);
    r = (int)(r * factor); if (r < 0) r = 0; if (r > 255) r = 255;
    g = (int)(g * factor); if (g < 0) g = 0; if (g > 255) g = 255;
    b = (int)(b * factor); if (b < 0) b = 0; if (b > 255) b = 255;
    return RGBA(r, g, b, a);
}

/* ---- Panels ----------------------------------------------------------- */

static void draw_circle_pad(const GuiState *st) {
    C2D_DrawRectSolid(CP_PX, 0, 0.1f, CP_PW, 150, PANEL_DARK);

    C2D_DrawCircleSolid((float)CP_CX, (float)CP_CY, 0.2f,
                        (float)CP_R + 3.0f, GRID_DARK);
    C2D_DrawCircleSolid((float)CP_CX, (float)CP_CY, 0.21f,
                        (float)CP_R - 3.0f, PANEL_DARK);

    bool diff = (st->sel_note != st->played_note);

    for (int i = 0; i < NUM_NOTES; i++) {
        float ang = (float)(90 - i * 30) * (float)M_PI / 180.0f;
        float nx  = (float)CP_CX + (float)CP_R * cosf(ang);
        float ny  = (float)CP_CY - (float)CP_R * sinf(ang);

        bool  is_preview   = (i == st->sel_note);
        bool  is_committed = (i == st->played_note);
        u32   col          = dim_color(NOTE_COLOR[i], 0.45f);
        float r            = 7.5f;
        u32   text_col     = WHITE;

        if (is_committed) {
            col = NOTE_COLOR[i];
            r   = 11.0f;
            text_col = PANEL_DARK;
        }
        C2D_DrawCircleSolid(nx, ny, 0.3f, r, col);

        /* Preview ring around the hovered note when uncommitted */
        if (is_preview && diff) {
            C2D_DrawCircleSolid(nx, ny, 0.28f, 12.0f, PREVIEW_COL);
            C2D_DrawCircleSolid(nx, ny, 0.29f, 9.0f,  PANEL_DARK);
        }

        float lw, lh;
        C2D_TextGetDimensions(&noteText[i], 0.36f, 0.36f, &lw, &lh);
        C2D_DrawText(&noteText[i], C2D_WithColor,
                     nx - lw * 0.5f, ny - lh * 0.5f, 0.4f, 0.36f, 0.36f,
                     text_col);
    }

    float sx = (float)st->stick_x / 156.0f;
    float sy = (float)st->stick_y / 156.0f;
    if (sx < -1) sx = -1;
    if (sx >  1) sx =  1;
    if (sy < -1) sy = -1;
    if (sy >  1) sy =  1;
    float dotx = (float)CP_CX + sx * (float)(CP_R - 6);
    float doty = (float)CP_CY - sy * (float)(CP_R - 6);
    C2D_DrawCircleSolid(dotx, doty, 0.35f, 4.0f, ACCENT_Y);
}

static void draw_button_diamond(const GuiState *st) {
    C2D_DrawRectSolid(BTN_PX, 0, 0.1f, BTN_PW, 150, PANEL_DARK);

    int b_var = st->dim_mode ? VAR_DIM : VAR_SUS;
    struct { int dx, dy, var; } btn[4] = {
        {  0, -1, VAR_MAJ },   /* X */
        {  1,  0, VAR_MIN },   /* A */
        {  0,  1, b_var   },   /* B */
        { -1,  0, VAR_7   },   /* Y */
    };

    bool diff = (st->sel_variant != st->played_variant);

    for (int i = 0; i < 4; i++) {
        float bx = (float)BTN_CX + (float)(btn[i].dx * BTN_R);
        float by = (float)BTN_CY + (float)(btn[i].dy * BTN_R);
        bool  is_preview   = (btn[i].var == st->sel_variant);
        bool  is_committed = (btn[i].var == st->played_variant);

        u32 ring  = MID_GREY;
        u32 inner = PANEL_DARK;
        if (is_committed) {
            ring  = ACCENT_Y;
            inner = dim_color(NOTE_COLOR[st->played_note], 0.85f);
        }
        C2D_DrawCircleSolid(bx, by, 0.3f, (float)BTN_RAD, ring);
        C2D_DrawCircleSolid(bx, by, 0.31f, (float)BTN_RAD - 2.0f, inner);

        if (is_preview && diff) {
            C2D_DrawCircleSolid(bx, by, 0.28f, (float)BTN_RAD + 3.0f, PREVIEW_COL);
            C2D_DrawCircleSolid(bx, by, 0.29f, (float)BTN_RAD - 1.0f, PANEL_DARK);
        }

        float lw, lh;
        C2D_TextGetDimensions(&varLabel[btn[i].var], 0.42f, 0.42f, &lw, &lh);
        C2D_DrawText(&varLabel[btn[i].var], C2D_WithColor,
                     bx - lw * 0.5f, by - lh * 0.5f,
                     0.4f, 0.42f, 0.42f,
                     is_committed ? WHITE : LIGHT_GREY);
    }
}

/* D-pad widget: octave selection (preview/commit like the variant buttons).
 *   Up    -> +1     Down  -> -1
 *   Right ->  0     Left  -> +2   */
static void draw_dpad(const GuiState *st) {
    C2D_DrawRectSolid(DPAD_PX, 0, 0.1f, DPAD_PW, 150, PANEL_DARK);

    float cx = (float)DPAD_CX, cy = (float)DPAD_CY;
    float w = (float)DARM_W, h = (float)DARM_H;

    bool diff = (st->sel_octave != st->played_octave);

    /* Map each arm to its octave value */
    int arm_oct[4] = { +1, -1, +2, 0 };   /* up, down, left, right */

    /* Arm rectangles in (x, y, w, h) tuples */
    struct { float x, y, w, h; } arm[4] = {
        { cx - w*0.5f,        cy - w*0.5f - h, w, h },   /* up    */
        { cx - w*0.5f,        cy + w*0.5f,     w, h },   /* down  */
        { cx - w*0.5f - h,    cy - w*0.5f,     h, w },   /* left  */
        { cx + w*0.5f,        cy - w*0.5f,     h, w },   /* right */
    };
    /* Label centers */
    struct { float x, y; } lblCenter[4] = {
        { cx,                  cy - w*0.5f - h*0.5f },   /* up    */
        { cx,                  cy + w*0.5f + h*0.5f },   /* down  */
        { cx - w*0.5f - h*0.5f, cy                   },  /* left  */
        { cx + w*0.5f + h*0.5f, cy                   },  /* right */
    };
    C2D_Text *lblText[4] = {
        &octaveText[3],   /* +1 */
        &octaveText[1],   /* -1 */
        &octaveText[4],   /* +2 */
        &octaveText[2],   /*  0 */
    };

    for (int i = 0; i < 4; i++) {
        bool is_committed = (arm_oct[i] == st->played_octave);
        bool is_preview   = (arm_oct[i] == st->sel_octave);
        u32  col          = is_committed ? ACCENT_Y : MID_GREY;
        C2D_DrawRectSolid(arm[i].x, arm[i].y, 0.2f, arm[i].w, arm[i].h, col);

        if (is_preview && diff) {
            /* Peach outline ring on the previewed arm */
            float ox = arm[i].x - 2.0f;
            float oy = arm[i].y - 2.0f;
            float ow = arm[i].w + 4.0f;
            float oh = arm[i].h + 4.0f;
            C2D_DrawRectSolid(ox, oy, 0.19f, ow, oh, PREVIEW_COL);
            C2D_DrawRectSolid(arm[i].x, arm[i].y, 0.21f, arm[i].w, arm[i].h, col);
        }

        float lw, lh;
        C2D_TextGetDimensions(lblText[i], 0.40f, 0.40f, &lw, &lh);
        C2D_DrawText(lblText[i], C2D_WithColor,
                     lblCenter[i].x - lw * 0.5f,
                     lblCenter[i].y - lh * 0.5f,
                     0.4f, 0.40f, 0.40f,
                     is_committed ? PANEL_DARK : WHITE);
    }

    /* Center square */
    C2D_DrawRectSolid(cx - w*0.5f, cy - w*0.5f, 0.22f, w, w, MID_GREY);

    /* "OCT" tag below */
    static C2D_Text octLabel;
    static bool inited = false;
    if (!inited) {
        C2D_TextParse(&octLabel, textBuf, "OCT");
        C2D_TextOptimize(&octLabel);
        inited = true;
    }
    draw_text_centered(&octLabel, (float)DPAD_CX, 128.0f, 0.40f, LIGHT_GREY);
}

static void draw_cstick(const GuiState *st) {
    C2D_DrawRectSolid(CS_PX, 0, 0.1f, CS_PW, 150, PANEL_DARK);

    float depth = st->vibrato_depth;
    if (depth < 0) depth = 0;
    if (depth > 1) depth = 1;

    C2D_DrawCircleSolid((float)CS_CX, (float)CS_CY, 0.2f,
                        (float)CS_R + 3.0f, GRID_DARK);
    C2D_DrawCircleSolid((float)CS_CX, (float)CS_CY, 0.21f,
                        (float)CS_R - 3.0f, PANEL_DARK);
    float inner_r = 4.0f + (float)(CS_R - 6) * depth;
    u32 fill = dim_color(VIB_COL, 0.25f + 0.75f * depth);
    C2D_DrawCircleSolid((float)CS_CX, (float)CS_CY, 0.22f, inner_r, fill);

    float sx = (float)st->cstick_x / 156.0f;
    float sy = (float)st->cstick_y / 156.0f;
    if (sx < -1) sx = -1;
    if (sx >  1) sx =  1;
    if (sy < -1) sy = -1;
    if (sy >  1) sy =  1;
    float dotx = (float)CS_CX + sx * (float)(CS_R - 6);
    float doty = (float)CS_CY - sy * (float)(CS_R - 6);
    C2D_DrawCircleSolid(dotx, doty, 0.35f, 5.0f,
                        depth > 0.05f ? WHITE : MID_GREY);

    draw_text_centered(&vibText, (float)CS_CX, 128.0f, 0.45f,
                       depth > 0.05f ? VIB_COL : LIGHT_GREY);
}

/* ---- Top screen ------------------------------------------------------- */

void gui_draw_top(const GuiState *st) {
    /* Header */
    C2D_DrawRectSolid(0, 0, 0.05f, 400, 24, PANEL_DARK);
    draw_text_centered(&titleText, 200.0f, 4.0f, 0.55f, WHITE);

    /* HOLD / LOCK badge in top-right. Always visible; brighter when a
     * chord is currently sustaining. */
    C2D_Text *modeText = st->r_mode_lock ? &lockText : &holdText;
    u32       modeBg   = st->r_mode_lock ? LOCK_COL  : HOLD_GLOW;
    bool      active   = st->hold_active;
    float     mode_w;
    {
        float h;
        C2D_TextGetDimensions(modeText, 0.45f, 0.45f, &mode_w, &h);
        u32 bg = active ? modeBg : dim_color(modeBg, 0.45f);
        C2D_DrawRectSolid(400.0f - mode_w - 16.0f, 4, 0.06f,
                          mode_w + 12, 18, bg);
        C2D_DrawText(modeText, C2D_WithColor,
                     400.0f - mode_w - 10.0f, 6, 0.07f, 0.45f, 0.45f,
                     active ? WHITE : LIGHT_GREY);
    }

    /* "L = set" badge when preview differs from committed */
    bool needs_commit = (st->sel_note    != st->played_note    ||
                        st->sel_variant != st->played_variant ||
                        st->sel_octave  != st->played_octave);
    if (needs_commit) {
        float w, h;
        C2D_TextGetDimensions(&pressLText, 0.40f, 0.40f, &w, &h);
        float bx = 400.0f - mode_w - 16.0f - w - 18.0f;
        C2D_DrawRectSolid(bx, 4, 0.06f, w + 12, 18, PREVIEW_COL);
        C2D_DrawText(&pressLText, C2D_WithColor,
                     bx + 6, 6, 0.07f, 0.40f, 0.40f, PANEL_DARK);
    }

    draw_circle_pad(st);
    draw_button_diamond(st);
    draw_dpad(st);
    draw_cstick(st);

    /* Big chord display strip — committed chord only */
    int chord_idx = st->played_note * NUM_VARIANTS + st->played_variant;
    u32 chord_col = NOTE_COLOR[st->played_note];

    C2D_DrawRectSolid(0, 156, 0.05f, 400, 64, PANEL_DARK);
    if (st->hold_active) {
        C2D_DrawRectSolid(0, 154, 0.04f, 400, 2, modeBg);
        C2D_DrawRectSolid(0, 220, 0.04f, 400, 2, modeBg);
    }
    draw_text_centered(&chordNameText[chord_idx], 200.0f, 158.0f, 1.10f,
                       chord_col);
    draw_text_centered(&breakdownText[chord_idx], 200.0f, 222.0f, 0.45f,
                       LIGHT_GREY);
}

/* ---- Bottom screen ----------------------------------------------------
 * Right half: 12-slot vertical strum bar (slot 0 = lowest at bottom,
 * slot 11 = highest at top). Uniform slot size. Left half intentionally
 * blank for future content.
 * ---------------------------------------------------------------------- */

void gui_draw_bottom(const GuiState *st) {
    /* Background — leave left half visually blank (just bg color). */

    int chord_size = audio_chord_size(st->played_variant);
    if (chord_size < 1) chord_size = 1;

    for (int i = 0; i < NUM_STRUM_NOTES; i++) {
        /* slot i: bottom = lowest pitch */
        float by = (float)STRUM_Y + (float)STRUM_H
                 - (float)(i + 1) * STRUM_SLOT_H + 1.0f;
        float bh = STRUM_SLOT_H - 2.0f;

        int tone = i % chord_size;
        int semi = audio_chord_semi(st->played_variant, tone);
        int note = (st->played_note + semi) % 12;
        u32 col  = NOTE_COLOR[note];

        bool active = (i == st->last_strum_slot);
        u32  fill   = active ? col : dim_color(col, 0.72f);
        C2D_DrawRectSolid((float)STRUM_X, by, 0.2f,
                          (float)STRUM_W, bh, fill);

        if (active) {
            C2D_DrawRectSolid((float)STRUM_X - 2.0f, by, 0.21f,
                              2.0f, bh, WHITE);
        }

        /* Note label, vertically centered in the slot */
        float lw, lh;
        C2D_TextGetDimensions(&noteText[note], 0.55f, 0.55f, &lw, &lh);
        C2D_DrawText(&noteText[note], C2D_WithColor,
                     (float)STRUM_X + ((float)STRUM_W - lw) * 0.5f,
                     by + (bh - lh) * 0.5f,
                     0.3f, 0.55f, 0.55f,
                     active ? PANEL_DARK : WHITE);
    }
    (void)BOTTOM_BG;
}

/* Touch hit-test for the vertical strum bar. Returns 0..11, or -1 if
 * the coordinates fall outside the bar. */
int gui_strum_slot_at(int x, int y) {
    if (x < STRUM_X || x > STRUM_X + STRUM_W) return -1;
    if (y < STRUM_Y || y > STRUM_Y + STRUM_H) return -1;
    /* y=top of bar => slot 11, y=bottom of bar => slot 0 */
    float rel = (float)(STRUM_Y + STRUM_H - y) / STRUM_SLOT_H;
    int slot = (int)rel;
    if (slot < 0) slot = 0;
    if (slot >= NUM_STRUM_NOTES) slot = NUM_STRUM_NOTES - 1;
    return slot;
}
