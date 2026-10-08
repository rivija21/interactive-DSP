#include "Theme.h"

const Theme& Theme::app() {
    static const Theme t{
        Color::hex(0xF3EDE2),        // background
        Color::hex(0xFFFDF8),        // panel
        Color::hex(0xE3D9C6),        // panelBorder
        Color::hex(0xFFFFFC),        // plotBackground
        Color::hex(0xEDF1EA),        // gridMinor
        Color::hex(0xD5DFD2),        // gridMajor
        Color::hex(0x7A705F),        // axisText
        Color::hex(0x2B2A33),        // text
        Color::hex(0x7C7364),        // secondaryText
        Color::hex(0x1C7C6A),        // output
        Color::hex(0xA79F92),        // input
        Color::hex(0x1F4E9E),        // response
        Color::hex(0x7B3F9E),        // phase
        Color::hex(0xC0392B),        // pole
        Color::hex(0x2675B8),        // zero
        Color::hex(0x3B3A44),        // unitCircle
        Color::hex(0xC0392B),        // danger
        Color::hex(0x2B2A33, 0.7f),  // cursor
        Color::hex(0xC27A1A),        // marker
    };
    return t;
}

const Theme& Theme::print() {
    static const Theme t = [] {
        Theme p = Theme::app();
        p.background = Color::white();
        p.panel = Color::white();
        p.plotBackground = Color::white();
        p.gridMinor = Color::hex(0xF0F1F3);
        p.gridMajor = Color::hex(0xD8DBE0);
        p.axisText = Color::hex(0x4B5563);
        p.text = Color::hex(0x111827);
        return p;
    }();
    return t;
}
