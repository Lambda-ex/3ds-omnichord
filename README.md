# 3DS Omnichord

A Nintendo 3DS homebrew app that turns the console into an [Omnichord](https://en.wikipedia.org/wiki/Omnichord) —
pick a chord, strum, and play.

*Only Supports New Nintendo 3DS

Built with devkitARM + libctru + citro2d. Runs on real hardware via the
Homebrew Launcher and produces its own sound via ndsp voice-pool synthesis
(no pre-recorded samples).

## Controls

| Action | Control |
| --- | --- |
| Preview note | Circle pad (snaps to 12 positions) |
| Preview variant | X = maj, A = min, Y = 7, B = sus/dim |
| Flip B between sus / dim | ZL |
| Preview octave | D-pad: Right = 0, Up = +1, Down = -1, Left = +2 |
| **Commit** note + variant + octave | **L** |
| Toggle R-mode (HOLD ↔ LOCK) | ZR |
| Play / sustain committed chord | R (HOLD: hold; LOCK: tap to toggle) |
| Vibrato (sustained voices only) | C-stick deflection |
| Pluck a strum slot | Touch or drag the vertical strum bar (bottom-right of touchscreen) |
| Quit | START |

Nothing plays until you press **L** — the circle pad, variant buttons and
D-pad only update a live preview, marked with a peach-colored ring. L
commits all three at once to the "playing chord."

## Building

Requires [devkitPro](https://devkitpro.org/) with `devkitARM`, `libctru`,
and `citro2d`.

From the devkitPro MSYS2 shell:

```bash
make
```

Copies to `omnichord.3dsx` in the project root. Drop that file into
`/3ds/` on your SD card and launch it from the Homebrew Launcher.

## Layout

```
source/
  main.c    — input loop, preview/commit state, render orchestration
  audio.c   — voice pool, strum scheduler, soft-saturating mixer, vibrato
  gui.c     — top-screen input widgets, bottom-screen strum bar
Makefile    — devkitARM build rules (uses $(DEVKITARM)/3ds_rules)
```
