#include "TheoryPanel.h"
#include "../app/Export.h"
#include "../app/Lessons.h"
#include "../platform/Dispatch.h"
#include "../util/Format.h"
#include "../util/Text.h"
#include <algorithm>
#include <cmath>
#include <map>

// MARK: - Text styles

namespace T {

Font bodyFont() { return Fonts::system(12.5f); }
Font mathFont() { return Fonts::math(13.5f); }

/// The serif fonts lack some Unicode super/subscript glyphs, so turn z⁻¹ into z^{−1}
/// and b₀ into b_{0}, which `rich` draws with a raised or lowered baseline.
std::string markup(const std::string& s) {
    static const std::map<char32_t, char32_t> sup = {{U'⁰', U'0'}, {U'¹', U'1'}, {U'²', U'2'}, {U'³', U'3'}, {U'⁴', U'4'},
                                                     {U'⁵', U'5'}, {U'⁶', U'6'}, {U'⁷', U'7'}, {U'⁸', U'8'}, {U'⁹', U'9'},
                                                     {U'⁻', U'−'}, {U'ⁿ', U'n'}};
    static const std::map<char32_t, char32_t> sub = {{U'₀', U'0'}, {U'₁', U'1'}, {U'₂', U'2'}, {U'₃', U'3'}, {U'₄', U'4'},
                                                     {U'₅', U'5'}, {U'₆', U'6'}, {U'₇', U'7'}, {U'₈', U'8'}, {U'₉', U'9'},
                                                     {U'ₖ', U'k'}, {U'ᵢ', U'i'}};
    std::u32string in = toU32(s), out, run;
    char32_t mode = 0;
    auto flush = [&] {
        if (mode && !run.empty()) {
            out.push_back(mode);
            out.push_back(U'{');
            out += run;
            out.push_back(U'}');
        }
        run.clear();
        mode = 0;
    };
    for (char32_t c : in) {
        auto a = sup.find(c);
        auto b = sub.find(c);
        if (a != sup.end()) {
            if (mode != U'^') {
                flush();
                mode = U'^';
            }
            run.push_back(a->second);
        } else if (b != sub.end()) {
            if (mode != U'_') {
                flush();
                mode = U'_';
            }
            run.push_back(b->second);
        } else {
            flush();
            out.push_back(c);
        }
    }
    flush();
    return fromU32(out);
}

/// Supports x^{sup} and x_{sub} markup.
std::vector<TextRun> rich(const std::string& s, const Font& font, Color color) {
    std::vector<TextRun> runs;
    std::string plain;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if ((c == '^' || c == '_') && i + 1 < s.size() && s[i + 1] == '{') {
            size_t close = s.find('}', i + 2);
            if (close != std::string::npos) {
                if (!plain.empty()) runs.push_back({plain, font, color});
                plain.clear();
                runs.push_back({s.substr(i + 2, close - i - 2), font, color, "", c == '^' ? 1 : -1});
                i = close + 1;
                continue;
            }
        }
        plain.push_back(c);
        i++;
    }
    if (!plain.empty() || runs.empty()) runs.push_back({plain, font, color});
    return runs;
}

Paragraph para(std::vector<TextRun> runs, float before, float after, float indent = 0, float lineSpacing = 2) {
    Paragraph p;
    p.runs = std::move(runs);
    p.spacingBefore = before;
    p.spacingAfter = after;
    p.indent = indent;
    p.lineSpacing = lineSpacing;
    return p;
}

Paragraph h1(const std::string& s) { return para({{s, Fonts::title(), Theme::app().text}}, 0, 4); }

Paragraph h2(const std::string& s) { return para({{uppercased(s), Fonts::header(), Theme::heading()}}, 12, 6); }

Paragraph body(const std::string& s, Color color = Theme::app().text) { return para(rich(s, bodyFont(), color), 0, 8); }

Paragraph secondary(const std::string& s) { return para(rich(s, Fonts::system(11.5f), Theme::app().secondaryText), 0, 8); }

Paragraph bullet(const std::string& s) {
    Paragraph p = para(rich(s, bodyFont(), Theme::app().text), 0, 6);
    p.bullet = true;
    return p;
}

Paragraph math(const std::string& s) { return para(rich(markup(s), mathFont(), Theme::ink()), 2, 10, 10); }

void code(std::vector<Paragraph>& out, const std::string& s) {
    size_t start = 0;
    while (true) {
        size_t end = s.find('\n', start);
        std::string line = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
        out.push_back(para({{line, Fonts::monoSmall(), Theme::app().text}}, 0, 4, 4));
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

TextRun inlineRun(const std::string& s, Font font = bodyFont(), Color color = Theme::app().text) { return {s, font, color}; }

TextRun link(const std::string& title, const std::string& url, bool bold = false) {
    return {title, bold ? Fonts::system(13, FontWeight::semibold) : Fonts::system(12.5f, FontWeight::medium), Theme::app().output, url};
}

Paragraph spaced(std::vector<TextRun> runs, float before) { return para(std::move(runs), before, 2); }

/// A link on its own line with the default paragraph style.
Paragraph linkLine(const std::string& title, const std::string& url) { return para({link(title, url)}, 0, 0, 0, 0); }

} // namespace T

// MARK: - Rich text view

void RichTextView::setParagraphs(std::vector<Paragraph> p) {
    paragraphs_ = std::move(p);
    laidWidth_ = -1;
    hasSelection_ = false;
    selecting_ = false;
    setNeedsDisplay();
}

void RichTextView::rebuild(float w) {
    if (w == laidWidth_) return;
    laid_.clear();
    tops_.clear();
    float y = kInsetY;
    for (size_t i = 0; i < paragraphs_.size(); i++) {
        auto rp = std::make_unique<RichParagraph>(paragraphs_[i], std::max(40.0f, w - 2 * kInsetX));
        // TextKit ignores the space before the first paragraph.
        if (i > 0) y += paragraphs_[i].spacingBefore;
        tops_.push_back(y);
        y += rp->height() + paragraphs_[i].spacingAfter;
        laid_.push_back(std::move(rp));
    }
    contentHeight_ = y + kInsetY;
    laidWidth_ = w;
}

float RichTextView::heightForWidth(float w) {
    rebuild(w);
    return std::ceil(contentHeight_);
}

void RichTextView::draw(Canvas& c) {
    rebuild(frame.w);
    Pos a = anchor_, b = head_;
    if (b < a) std::swap(a, b);
    for (size_t i = 0; i < laid_.size(); i++) {
        float top = tops_[i];
        if (top > frame.h || top + laid_[i]->height() < 0) continue;
        if (hasSelection_ && i >= a.para && i <= b.para) {
            UINT32 from = i == a.para ? a.offset : 0;
            UINT32 to = i == b.para ? b.offset : UINT32(laid_[i]->text().size());
            for (const Rect& r : laid_[i]->selectionRects(from, to))
                c.fillRect(r.offsetBy(kInsetX, top), Color::hex(0xB3D7FF));
        }
        c.drawParagraph(*laid_[i], {kInsetX, top});
    }
}

std::string RichTextView::linkAt(Point p) {
    rebuild(frame.w);
    for (size_t i = 0; i < laid_.size(); i++) {
        float top = tops_[i];
        if (p.y >= top && p.y < top + laid_[i]->height()) return laid_[i]->linkAt({p.x - kInsetX, p.y - top});
    }
    return {};
}

RichTextView::Pos RichTextView::positionAt(Point p) {
    rebuild(frame.w);
    if (laid_.empty()) return {};
    if (p.y < tops_.front()) return {0, 0};
    for (size_t i = 0; i < laid_.size(); i++) {
        float top = tops_[i];
        float bottom = i + 1 < laid_.size() ? tops_[i + 1] : top + laid_[i]->height();
        if (p.y < bottom) return {i, laid_[i]->positionAt({p.x - kInsetX, std::min(p.y - top, laid_[i]->height() - 1)})};
    }
    return {laid_.size() - 1, UINT32(laid_.back()->text().size())};
}

Cursor RichTextView::cursorAt(Point p) { return linkAt(p).empty() ? Cursor::ibeam : Cursor::hand; }

void RichTextView::mouseMoved(const MouseEvent&) {}

void RichTextView::mouseDown(const MouseEvent& e) {
    pressedLink_ = linkAt(e.location);
    if (!pressedLink_.empty()) return;
    anchor_ = head_ = positionAt(e.location);
    selecting_ = true;
    if (hasSelection_) {
        hasSelection_ = false;
        setNeedsDisplay();
    }
}

void RichTextView::mouseDragged(const MouseEvent& e) {
    if (!selecting_) return;
    head_ = positionAt(e.location);
    hasSelection_ = !(anchor_ == head_);
    setNeedsDisplay();
}

void RichTextView::mouseUp(const MouseEvent& e) {
    selecting_ = false;
    if (!pressedLink_.empty()) {
        std::string link = pressedLink_;
        pressedLink_.clear();
        if (linkAt(e.location) == link && onLink) onLink(link);
    }
}

bool RichTextView::hasSelection() const { return hasSelection_; }

std::string RichTextView::selectedText() const {
    if (!hasSelection_) return {};
    Pos a = anchor_, b = head_;
    if (b < a) std::swap(a, b);
    std::vector<std::string> lines;
    for (size_t i = a.para; i <= b.para && i < laid_.size(); i++) {
        const std::wstring& t = laid_[i]->text();
        size_t from = i == a.para ? a.offset : 0;
        size_t to = i == b.para ? b.offset : t.size();
        std::string line = narrow(t.substr(from, to - from));
        if (laid_[i]->source().bullet && from == 0) line = "\xE2\x80\xA2\t" + line;
        lines.push_back(line);
    }
    return join(lines, "\n");
}

void RichTextView::selectAll() {
    if (laid_.empty()) return;
    anchor_ = {0, 0};
    head_ = {laid_.size() - 1, UINT32(laid_.back()->text().size())};
    hasSelection_ = true;
    setNeedsDisplay();
}

// MARK: - Theory panel

TheoryPanel::TheoryPanel(LabModel& model) : pageControl({"This filter", "Lessons", "Experiments"}, ControlSize::smallSize), model_(model) {
    pageControl.onChange = [this](int i) {
        switch (i) {
        case 0: model_.setTheoryPage(TheoryPage::filter()); break;
        case 1: model_.setTheoryPage(TheoryPage::lessons()); break;
        default: model_.setTheoryPage(TheoryPage::experiments()); break;
        }
    };
    text_.onLink = [this](const std::string& url) { linkClicked(url); };
    addChild(&scroll_);
    model_.observe([this](ModelChange change) {
        if (intersects(change, Change::filter | Change::zeros | Change::theory | Change::sampleRate))
            scheduleRefresh((change & Change::theory) != 0);
    });
    refresh(true);
}

void TheoryPanel::layout() {
    scroll_.setFrame(bounds());
    scroll_.layout();
}

void TheoryPanel::scheduleRefresh(bool scrollToTop) {
    if (scrollToTop) {
        refresh(true);
        return;
    }
    if (pending_) return;
    pending_ = true;
    dispatchMainAfter(0.12, [this] {
        pending_ = false;
        refresh(false);
    });
}

void TheoryPanel::refresh(bool scrollToTop) {
    std::vector<Paragraph> content;
    const TheoryPage& page = model_.theoryPage();
    switch (page.kind) {
    case TheoryPage::Kind::filter:
        pageControl.setSelected(0);
        content = filterPage();
        break;
    case TheoryPage::Kind::lessons:
        pageControl.setSelected(1);
        content = lessonsPage();
        break;
    case TheoryPage::Kind::lesson:
        pageControl.setSelected(1);
        content = lessonPage(page.lessonID);
        break;
    case TheoryPage::Kind::experiments:
        pageControl.setSelected(2);
        content = experimentsPage();
        break;
    }
    float visible = scroll_.offset();
    text_.setParagraphs(std::move(content));
    scroll_.documentChanged();
    if (scrollToTop) scroll_.scrollToTop();
    else scroll_.setOffset(visible);
    setNeedsDisplay();
}

void TheoryPanel::linkClicked(const std::string& url) {
    if (!hasPrefix(url, "lab:")) return;
    std::string rest = url.substr(4);
    size_t slash = rest.find('/');
    std::string kind = rest.substr(0, slash);
    std::string arg = slash == std::string::npos ? "" : rest.substr(slash + 1);
    if (kind == "lesson") {
        model_.setTheoryPage(TheoryPage::lesson(arg));
    } else if (kind == "page") {
        model_.setTheoryPage(arg == "lessons" ? TheoryPage::lessons()
                                              : (arg == "experiments" ? TheoryPage::experiments() : TheoryPage::filter()));
    } else if (kind == "try") {
        model_.runExperiment(arg);
    } else if (kind == "copy") {
        for (auto lang : kAllLanguages) {
            if (arg != languageRawValue(lang)) continue;
            copyToClipboard(model_.window, exportCode(lang, model_.spec(), model_.filter()));
            if (onCopied) onCopied(std::string("Copied ") + languageTitle(lang) + " code to the clipboard");
        }
    }
}

// MARK: Pages

namespace {

std::string conceptLesson(const DesignSpec& s) {
    switch (s.method) {
    case DesignMethod::iir: return s.family == IIRFamily::bessel ? "phase" : "families";
    case DesignMethod::fir: return "fir";
    case DesignMethod::poleZero: return "zplane";
    }
    return "zplane";
}

std::string conceptLinkTitle(const DesignSpec& s) {
    const Lesson* l = Lessons::lesson(conceptLesson(s));
    return l ? "Lesson: " + l->title + " \xE2\x86\x92" : "";
}

std::string conceptText(const DesignSpec& s) {
    switch (s.method) {
    case DesignMethod::iir: {
        std::string base = std::string("Designed as an analog ") + familyTitle(s.family) + " prototype, shifted to the " +
                           lowercased(bandTitle(s.band)) + " band, then mapped to the z-plane with the bilinear transform.";
        switch (s.family) {
        case IIRFamily::butterworth: return base + " Butterworth is as flat as possible in the passband.";
        case IIRFamily::chebyshev1: return base + " Chebyshev I trades passband ripple for steepness.";
        case IIRFamily::chebyshev2:
            return base + " Chebyshev II keeps the passband flat and puts its ripple, and its zeros, in the stopband.";
        case IIRFamily::elliptic: return base + " Elliptic ripples in both bands to get the sharpest cut.";
        case IIRFamily::bessel: return base + " Bessel keeps the group delay as flat as possible.";
        }
        return base;
    }
    case DesignMethod::fir:
        return std::string("A truncated ideal (sinc) impulse response, smoothed by a ") + windowTitle(s.window) + " window.";
    case DesignMethod::poleZero:
        if (s.presetName)
            if (auto p = presetNamed(*s.presetName)) return presetSummary(*p);
        return "Every pole and zero was placed by hand. |H| at each frequency is the product of the distances to the zeros "
               "divided by the product of the distances to the poles.";
    }
    return "";
}

std::string poly(const std::vector<double>& c, bool leadingOne) {
    std::string s;
    for (size_t k = 0; k < c.size(); k++) {
        double v = c[k];
        if (v == 0 && k != 0) continue;
        std::string power = k == 0 ? "" : (k == 1 ? "z\xE2\x81\xBB\xC2\xB9" : "z\xE2\x81\xBB\xC2\xB2");
        std::string coefficient = std::fabs(std::fabs(v) - 1) < 1e-12 && k > 0 ? "" : fmt(std::fabs(v)) + " ";
        if (s.empty()) {
            s = (k == 0 && leadingOne ? std::string("1") : fmt(v)) + (power.empty() ? "" : " " + power);
        } else {
            s += (v < 0 ? std::string(" ") + kMinus + " " : std::string(" + ")) + coefficient + power;
        }
    }
    return s.empty() ? "0" : s;
}

/// A section written as a stacked fraction, textbook style, with the numerator's
/// leading coefficient pulled out as a gain so the lines stay short.
std::string fractionString(const Biquad& s, const std::string& label) {
    std::vector<double> b{s.b0, s.b1, s.b2};
    double gain = 1.0;
    for (double v : b) {
        if (v != 0) {
            if (std::fabs(v - 1) > 1e-12) {
                gain = v;
                for (auto& x : b) x /= v;
            }
            break;
        }
    }
    std::string num = poly(b, false);
    std::string den = poly({1, s.a1, s.a2}, true);
    size_t width = std::max(utf8Length(num), utf8Length(den));
    std::string head = label + " = " + (gain == 1 ? "" : fmt(gain) + " \xC2\xB7 ");
    std::string pad(utf8Length(head), ' ');
    auto centred = [&](const std::string& t) { return std::string((width - utf8Length(t)) / 2, ' ') + t; };
    return pad + centred(num) + "\n" + head + repeated("\xE2\x94\x80", width) + "\n" + pad + centred(den);
}

std::string term(double v, const std::string& name, bool first) {
    if (first) return fmt(v) + "\xC2\xB7" + name;
    return (v < 0 ? std::string(" ") + kMinus + " " : std::string(" + ")) + fmt(std::fabs(v)) + "\xC2\xB7" + name;
}

std::string rootTable(const ZPK& zpk, double fs) {
    std::vector<std::string> lines;
    std::pair<std::string, const std::vector<Complex>*> sets[] = {{"\xC3\x97  pole", &zpk.poles}, {"\xE2\x97\x8B  zero", &zpk.zeros}};
    for (const auto& [kind, roots] : sets) {
        std::vector<std::pair<Complex, int>> groups;
        for (const auto& r : *roots) {
            if (!(r.im >= -1e-12 || r.isReal(1e-9))) continue;
            bool found = false;
            for (auto& g : groups) {
                if ((g.first - r).magnitude() < 1e-6 * std::max(1.0, r.magnitude())) {
                    g.second += 1;
                    found = true;
                    break;
                }
            }
            if (!found) groups.push_back({r, 1});
        }
        std::stable_sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return a.first.phase() < b.first.phase(); });
        const size_t limit = 10;
        for (size_t i = 0; i < groups.size() && i < limit; i++) {
            Complex z = groups[i].first;
            int n = groups[i].second;
            std::string mult = n > 1 ? " \xC3\x97" + std::to_string(n) : "";
            if (z.isReal(1e-9)) {
                std::string note = z.magnitude() < 1e-12 ? " (origin)"
                                                         : (std::fabs(z.re + 1) < 1e-9 ? " (Nyquist)" : (std::fabs(z.re - 1) < 1e-9 ? " (DC)" : ""));
                lines.push_back(kind + "  z = " + formatSigned(z.re, "%.5f") + mult + note);
            } else {
                double f = std::fabs(z.phase()) / (2 * kPi) * fs;
                lines.push_back(kind + strf("  r = %.5f  \xC2\xB1%6.2f\xC2\xB0  ", z.magnitude(), std::fabs(z.phase()) * 180 / kPi) +
                                formatHz(f) + mult);
            }
        }
        if (groups.size() > limit) {
            std::u32string k = toU32(kind);
            lines.push_back("   \xE2\x80\xA6 " + std::to_string(groups.size() - limit) + " more " + fromU32(k.substr(3)) + "s");
        }
    }
    return lines.empty() ? "No poles or zeros: H(z) is a constant." : join(lines, "\n");
}

} // namespace

std::vector<Paragraph> TheoryPanel::filterPage() {
    const DesignSpec& spec = model_.spec();
    const DigitalFilter& filter = model_.filter();
    double fs = model_.fs();
    std::vector<Paragraph> out;
    out.push_back(T::h1(filterTitle(spec)));
    out.push_back(T::secondary(filterSubtitle(spec, fs)));
    out.push_back(T::body(conceptText(spec) + " "));
    out.push_back(T::linkLine(conceptLinkTitle(spec), "lab:lesson/" + conceptLesson(spec)));

    // Transfer function.
    out.push_back(T::h2("Transfer function"));
    if (!filter.structure.isFIR) {
        const auto& sos = filter.structure.sos;
        if (sos.size() == 1) {
            T::code(out, fractionString(sos[0], "H(z)"));
        } else {
            out.push_back(T::math("H(z) = H₁(z) · H₂(z) ⋯ H" + subscriptDigits(int(sos.size())) + "(z)"));
            out.push_back(T::math("Hₖ(z) = (b₀ + b₁z⁻¹ + b₂z⁻²) / (1 + a₁z⁻¹ + a₂z⁻²)"));
            out.push_back(T::secondary("A cascade of " + std::to_string(sos.size()) +
                                       " second-order sections (biquads), which is how the audio is actually processed:"));
            for (size_t i = 0; i < sos.size() && i < 16; i++) T::code(out, fractionString(sos[i], "H" + subscriptDigits(int(i + 1))));
            if (sos.size() > 16) out.push_back(T::secondary("\xE2\x80\xA6 and " + std::to_string(sos.size() - 16) + " more sections"));
        }
    } else {
        const auto& h = filter.structure.taps;
        out.push_back(T::math("H(z) = Σₖ h[k] z^{−k},  k = 0 … " + std::to_string(h.size() - 1)));
        std::vector<std::string> shown;
        for (size_t i = 0; i < h.size() && i < 6; i++) shown.push_back(fmt(h[i]));
        T::code(out, "h = [" + join(shown, ", ") + (h.size() > 6 ? ", \xE2\x80\xA6]" : "]"));
        out.push_back(T::secondary("Symmetric taps, h[k] = h[N\xE2\x88\x92" "1\xE2\x88\x92k], so the phase is exactly linear."));
    }

    // Difference equation.
    out.push_back(T::h2("Difference equation"));
    if (!filter.structure.isFIR) {
        std::vector<double> b, a;
        sosToTransferFunction(filter.structure.sos, b, a);
        if (b.size() <= 5 && a.size() <= 5) {
            std::string eq = "y[n] = ";
            bool first = true;
            for (size_t k = 0; k < b.size(); k++) {
                if (b[k] == 0) continue;
                eq += term(b[k], "x[n" + (k == 0 ? std::string() : std::string(kMinus) + std::to_string(k)) + "]", first);
                first = false;
            }
            for (size_t k = 1; k < a.size(); k++) {
                if (a[k] == 0) continue;
                eq += term(-a[k], "y[n" + std::string(kMinus) + std::to_string(k) + "]", first);
                first = false;
            }
            T::code(out, eq);
            out.push_back(T::secondary("Feedback terms (y[n\xE2\x88\x92k]) are what make it IIR: the output depends on its own past."));
        } else {
            T::code(out, "y = b₀·x + s₁\ns₁ ← b₁·x − a₁·y + s₂\ns₂ ← b₂·x − a₂·y");
            out.push_back(T::secondary("Each section runs in transposed direct form II, and its output feeds the next. A single "
                                       "high-order polynomial would be numerically fragile, so the filter is never expanded into one."));
        }
    } else {
        T::code(out, "y[n] = Σₖ h[k]·x[n−k]   (" + std::to_string(filter.structure.taps.size()) + " multiplies per sample)");
        out.push_back(T::secondary("No feedback, so it is always stable. The cost is length: sharp FIR filters need many taps."));
    }

    // Poles and zeros.
    out.push_back(T::h2("Poles and zeros"));
    if (model_.zerosPending()) {
        out.push_back(T::secondary("Finding the zeros of the tap polynomial\xE2\x80\xA6"));
    } else {
        T::code(out, rootTable(filter.zpk, fs));
        if (filter.isStable()) {
            double r = filter.zpk.maxPoleRadius();
            out.push_back(T::secondary(r > 0 ? strf("All poles are inside the unit circle (largest |p| = %.4f), so the filter is "
                                                    "stable. It rings for about %.0f samples.",
                                                    r, 1 / std::max(1e-9, 1 - r))
                                             : "All poles are at the origin, so the filter is stable."));
        } else {
            out.push_back(T::body("A pole is on or outside the unit circle, so this filter is unstable.", Theme::app().danger));
        }
    }

    out.push_back(T::h2("Use it in your coursework"));
    out.push_back(T::secondary("Copy code that designs this filter, with the exact coefficients."));
    std::vector<TextRun> line;
    bool firstLang = true;
    for (auto lang : kAllLanguages) {
        if (!firstLang) line.push_back(T::inlineRun("   \xC2\xB7   ", T::bodyFont(), Theme::app().secondaryText));
        firstLang = false;
        line.push_back(T::link(std::string("Copy ") + languageTitle(lang), std::string("lab:copy/") + languageRawValue(lang)));
    }
    out.push_back(T::para(line, 0, 0, 0, 0));
    return out;
}

std::vector<Paragraph> TheoryPanel::lessonsPage() {
    std::vector<Paragraph> out;
    out.push_back(T::h1("Lessons"));
    out.push_back(T::secondary("Short explanations of what you're seeing. Each one links to experiments you can run."));
    for (const auto& lesson : Lessons::all()) {
        out.push_back(T::spaced({T::link(lesson.title, "lab:lesson/" + lesson.id, true)}, 10));
        out.push_back(T::secondary(lesson.summary));
    }
    return out;
}

std::vector<Paragraph> TheoryPanel::lessonPage(const std::string& id) {
    const Lesson* lesson = Lessons::lesson(id);
    if (!lesson) return lessonsPage();
    std::vector<Paragraph> out;
    out.push_back(T::linkLine("\xE2\x86\x90 All lessons", "lab:page/lessons"));
    out.push_back(T::h1(lesson->title));
    for (const auto& p : lesson->body) {
        if (hasPrefix(p, "\xE2\x80\xA2 ")) out.push_back(T::bullet(p.substr(4)));
        else if (hasPrefix(p, "= ")) out.push_back(T::math(p.substr(2)));
        else if (hasPrefix(p, "Try: ")) out.push_back(T::body("Try it: " + p.substr(5), Theme::app().output));
        else out.push_back(T::body(p));
    }
    if (!lesson->experiments.empty()) {
        out.push_back(T::h2("Experiments"));
        for (const auto& eid : lesson->experiments) {
            if (const Experiment* e = Experiments::experiment(eid))
                out.push_back(T::spaced({T::link("\xE2\x96\xB6 " + e->title, "lab:try/" + e->id)}, 4));
        }
    }
    return out;
}

std::vector<Paragraph> TheoryPanel::experimentsPage() {
    std::vector<Paragraph> out;
    out.push_back(T::h1("Experiments"));
    out.push_back(T::secondary("Each one sets up the source, the filter and the displays in one click. Turn the volume down first."));
    for (const auto& e : Experiments::all()) {
        bool running = model_.activeExperiment() && *model_.activeExperiment() == e.id;
        std::vector<TextRun> line{T::inlineRun(e.title, Fonts::system(12.5f, FontWeight::semibold),
                                               running ? Theme::app().output : Theme::app().text),
                                  T::inlineRun("   "),
                                  T::link(running ? "\xE2\x97\x8F running, set up again" : "Set it up \xE2\x96\xB8", "lab:try/" + e.id)};
        out.push_back(T::spaced(line, 12));
        out.push_back(T::secondary(e.description));
    }
    return out;
}
