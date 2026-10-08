#pragma once
#include "Graphics.h"

/// Colours for the on-screen (lab notebook) look and the exported (print) look.
struct Theme {
    Color background;
    Color panel;
    Color panelBorder;
    Color plotBackground;
    Color gridMinor;
    Color gridMajor;
    Color axisText;
    Color text;
    Color secondaryText;
    Color output;
    Color input;
    Color response;
    Color phase;
    Color pole;
    Color zero;
    Color unitCircle;
    Color danger;
    Color cursor;
    Color marker;

    /// The on-screen look: a classic lab notebook. Warm parchment, ivory panels,
    /// graph-paper grids, ink-blue curves and crimson poles.
    static const Theme& app();
    /// Exported figures: the same palette on pure white paper.
    static const Theme& print();
    /// Accent for headings in the theory panel.
    static Color heading() { return Color::hex(0xA0472E); }
    /// Equations.
    static Color ink() { return Color::hex(0x2A2440); }
};

namespace Fonts {
inline Font axis() { return Font{FontFamily::system, 9.5f, FontWeight::regular, true}; }
inline Font readout() { return Font{FontFamily::system, 11, FontWeight::medium, true}; }
inline Font smallText() { return Font{FontFamily::system, 11}; }
inline Font smallBold() { return Font{FontFamily::system, 11, FontWeight::semibold}; }
inline Font label() { return Font{FontFamily::system, 12}; }
inline Font header() { return Font{FontFamily::serif, 11.5f, FontWeight::semibold, false, 0.9f}; }
inline Font title() { return Font{FontFamily::serif, 17, FontWeight::semibold}; }
inline Font serif(float size, FontWeight w = FontWeight::regular) { return Font{FontFamily::serif, size, w}; }
inline Font math(float size) { return Font{FontFamily::math, size}; }
inline Font mono() { return Font{FontFamily::mono, 11}; }
inline Font monoSmall() { return Font{FontFamily::mono, 10.5f}; }
inline Font system(float size, FontWeight w = FontWeight::regular) { return Font{FontFamily::system, size, w}; }
} // namespace Fonts

/// Standard control colours (macOS light appearance).
namespace ControlColors {
inline Color accent() { return Color::hex(0x007AFF); }
inline Color label() { return Color{0, 0, 0, 0.85f}; }
inline Color secondaryLabel() { return Color{0, 0, 0, 0.5f}; }
inline Color bezel() { return Color::white(); }
inline Color bezelBorder() { return Color{0, 0, 0, 0.13f}; }
inline Color bezelShadow() { return Color{0, 0, 0, 0.12f}; }
} // namespace ControlColors
