#include "Icons.h"
#include <cmath>

namespace {

struct Grid {
    float ox, oy, s;
    Point p(float x, float y) const { return {ox + x * s, oy + y * s}; }
    Rect r(float x, float y, float w, float h) const { return {ox + x * s, oy + y * s, w * s, h * s}; }
};

void arc(Path& path, const Grid& g, float cx, float cy, float radius, float a0, float a1, bool move = true) {
    const int steps = 18;
    for (int i = 0; i <= steps; i++) {
        float a = a0 + (a1 - a0) * float(i) / steps;
        Point q = g.p(cx + radius * std::cos(a), cy + radius * std::sin(a));
        if (i == 0 && move) path.moveTo(q);
        else path.lineTo(q);
    }
}

void speakerBody(Canvas& c, const Grid& g, Color color) {
    Path body;
    body.moveTo(g.p(1.5f, 5.8f));
    body.lineTo(g.p(4.6f, 5.8f));
    body.lineTo(g.p(8.6f, 2.2f));
    body.lineTo(g.p(8.6f, 13.8f));
    body.lineTo(g.p(4.6f, 10.2f));
    body.lineTo(g.p(1.5f, 10.2f));
    body.close();
    c.fillPath(body, color);
}

} // namespace

void drawIcon(Canvas& c, Icon icon, Rect r, Color color) {
    float side = std::fmin(r.w, r.h);
    Grid g{r.midX() - side / 2, r.midY() - side / 2, side / 16.0f};
    const float lw = 1.35f * g.s;
    const auto round = D2D1_CAP_STYLE_ROUND;
    const auto roundJoin = D2D1_LINE_JOIN_ROUND;
    switch (icon) {
    case Icon::none: break;
    case Icon::share: {
        Path box;
        box.moveTo(g.p(5.5f, 6));
        box.lineTo(g.p(3, 6));
        box.lineTo(g.p(3, 15));
        box.lineTo(g.p(13, 15));
        box.lineTo(g.p(13, 6));
        box.lineTo(g.p(10.5f, 6));
        c.strokePath(box, color, lw, round, roundJoin);
        Path arrow;
        arrow.moveTo(g.p(8, 10.2f));
        arrow.lineTo(g.p(8, 1.2f));
        arrow.moveTo(g.p(5.2f, 4));
        arrow.lineTo(g.p(8, 1.2f));
        arrow.lineTo(g.p(10.8f, 4));
        c.strokePath(arrow, color, lw, round, roundJoin);
        break;
    }
    case Icon::zoomReset: {
        c.strokeEllipse(g.r(1.5f, 1.5f, 10.5f, 10.5f), color, lw);
        c.line(g.p(10.6f, 10.6f), g.p(14.6f, 14.6f), color, 2.0f * g.s, {}, round);
        Path arrows;
        arrows.moveTo(g.p(4.6f, 9));
        arrows.lineTo(g.p(9, 4.6f));
        arrows.moveTo(g.p(4.6f, 6.9f));
        arrows.lineTo(g.p(4.6f, 9));
        arrows.lineTo(g.p(6.7f, 9));
        arrows.moveTo(g.p(6.9f, 4.6f));
        arrows.lineTo(g.p(9, 4.6f));
        arrows.lineTo(g.p(9, 6.7f));
        c.strokePath(arrows, color, 1.1f * g.s, round, roundJoin);
        break;
    }
    case Icon::xmark:
        c.line(g.p(4, 4), g.p(12, 12), color, 1.6f * g.s, {}, round);
        c.line(g.p(12, 4), g.p(4, 12), color, 1.6f * g.s, {}, round);
        break;
    case Icon::pause:
        c.fillRoundedRect(g.r(3.6f, 2.5f, 3.2f, 11), 0.9f * g.s, color);
        c.fillRoundedRect(g.r(9.2f, 2.5f, 3.2f, 11), 0.9f * g.s, color);
        break;
    case Icon::play: {
        Path t;
        t.moveTo(g.p(4.5f, 2.4f));
        t.lineTo(g.p(13.4f, 8));
        t.lineTo(g.p(4.5f, 13.6f));
        t.close();
        c.fillPath(t, color);
        c.strokePath(t, color, 1.0f * g.s, round, roundJoin);
        break;
    }
    case Icon::speakerSlash:
        speakerBody(c, g, color);
        c.line(g.p(2, 1.8f), g.p(14.2f, 14.2f), color, 1.5f * g.s, {}, round);
        break;
    case Icon::speakerWave: {
        speakerBody(c, g, color);
        Path waves;
        arc(waves, g, 9, 8, 2.6f, -0.85f, 0.85f);
        arc(waves, g, 9, 8, 5.2f, -0.9f, 0.9f);
        c.strokePath(waves, color, lw, round, roundJoin);
        break;
    }
    case Icon::musicNote: {
        c.fillEllipse(g.r(2.3f, 10.2f, 5.4f, 4.2f), color);
        c.line(g.p(7.0f, 12.3f), g.p(7.0f, 2.2f), color, 1.4f * g.s, {}, round);
        Path flag;
        flag.moveTo(g.p(7.0f, 2.0f));
        flag.lineTo(g.p(12.6f, 3.6f));
        flag.lineTo(g.p(12.6f, 6.4f));
        flag.lineTo(g.p(7.0f, 4.8f));
        flag.close();
        c.fillPath(flag, color);
        break;
    }
    case Icon::waveform: {
        const float xs[] = {2, 4.4f, 6.8f, 9.2f, 11.6f, 14};
        const float hs[] = {3.5f, 8, 12, 6.5f, 9.5f, 3.5f};
        for (int i = 0; i < 6; i++) c.line(g.p(xs[i], 8 - hs[i] / 2), g.p(xs[i], 8 + hs[i] / 2), color, 1.4f * g.s, {}, round);
        break;
    }
    case Icon::squareOnSquare:
        c.strokeRoundedRect(g.r(5.2f, 1.8f, 9, 9), 1.8f * g.s, color, 1.2f * g.s);
        c.strokeRoundedRect(g.r(1.8f, 5.2f, 9, 9), 1.8f * g.s, color, 1.2f * g.s);
        break;
    case Icon::arrowUpRight: {
        Path a;
        a.moveTo(g.p(3.5f, 12.5f));
        a.lineTo(g.p(12.5f, 3.5f));
        a.moveTo(g.p(5.5f, 3.5f));
        a.lineTo(g.p(12.5f, 3.5f));
        a.lineTo(g.p(12.5f, 10.5f));
        c.strokePath(a, color, 1.5f * g.s, round, roundJoin);
        break;
    }
    case Icon::noise: {
        Path w;
        for (int row = 0; row < 3; row++) {
            float y0 = 4 + row * 4.0f;
            for (int i = 0; i <= 24; i++) {
                float x = 1.5f + 13.0f * float(i) / 24;
                float y = y0 + 1.1f * std::sin(float(i) * 1.3f + float(row) * 1.7f);
                if (i == 0) w.moveTo(g.p(x, y));
                else w.lineTo(g.p(x, y));
            }
        }
        c.strokePath(w, color, 1.2f * g.s, round, roundJoin);
        break;
    }
    case Icon::metronome: {
        Path body;
        body.moveTo(g.p(3.5f, 14.5f));
        body.lineTo(g.p(5.9f, 1.8f));
        body.lineTo(g.p(10.1f, 1.8f));
        body.lineTo(g.p(12.5f, 14.5f));
        body.close();
        c.strokePath(body, color, 1.2f * g.s, round, roundJoin);
        c.line(g.p(8, 11.5f), g.p(12.8f, 4.2f), color, 1.3f * g.s, {}, round);
        c.line(g.p(4.2f, 11.5f), g.p(11.8f, 11.5f), color, 1.1f * g.s, {}, round);
        break;
    }
    case Icon::mic: {
        c.fillRoundedRect(g.r(5.6f, 1.2f, 4.8f, 8.6f), 2.4f * g.s, color);
        Path u;
        arc(u, g, 8, 7.2f, 4.2f, 0.0f, 3.14159f);
        c.strokePath(u, color, 1.2f * g.s, round, roundJoin);
        c.line(g.p(8, 11.4f), g.p(8, 14.4f), color, 1.2f * g.s, {}, round);
        c.line(g.p(5.6f, 14.6f), g.p(10.4f, 14.6f), color, 1.2f * g.s, {}, round);
        break;
    }
    case Icon::doc: {
        Path page;
        page.moveTo(g.p(3.5f, 1.5f));
        page.lineTo(g.p(9.5f, 1.5f));
        page.lineTo(g.p(12.5f, 4.5f));
        page.lineTo(g.p(12.5f, 14.5f));
        page.lineTo(g.p(3.5f, 14.5f));
        page.close();
        page.moveTo(g.p(9.5f, 1.5f));
        page.lineTo(g.p(9.5f, 4.5f));
        page.lineTo(g.p(12.5f, 4.5f));
        c.strokePath(page, color, 1.2f * g.s, round, roundJoin);
        break;
    }
    }
}
