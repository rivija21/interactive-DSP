#pragma once
#include "Graphics.h"

/// Vector icons drawn in the style of the SF Symbols the macOS version uses.
enum class Icon {
    none,
    share,          // square.and.arrow.up
    zoomReset,      // arrow.up.left.and.down.right.magnifyingglass
    xmark,
    pause,          // pause.fill
    play,           // play.fill
    speakerSlash,   // speaker.slash.fill
    speakerWave,    // speaker.wave.2.fill
    musicNote,      // music.note
    waveform,
    squareOnSquare,
    arrowUpRight,
    noise,          // aqi.medium
    metronome,
    mic,
    doc,
};

/// Draws `icon` centred in `r` (designed on a 16 x 16 grid, scaled to fit).
void drawIcon(Canvas& c, Icon icon, Rect r, Color color);
