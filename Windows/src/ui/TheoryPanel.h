#pragma once
#include "../app/LabModel.h"
#include "Controls.h"
#include <functional>
#include <memory>

/// Selectable rich text with clickable links (the NSTextView in the theory panel).
class RichTextView : public Widget {
public:
    std::function<void(const std::string&)> onLink;
    void setParagraphs(std::vector<Paragraph> p);
    float heightForWidth(float w) override;
    void draw(Canvas& c) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDragged(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    void mouseMoved(const MouseEvent& e) override;
    Cursor cursorAt(Point p) override;
    bool hasSelection() const;
    std::string selectedText() const;
    void selectAll();

private:
    struct Pos {
        size_t para = 0;
        UINT32 offset = 0;
        bool operator<(const Pos& o) const { return para < o.para || (para == o.para && offset < o.offset); }
        bool operator==(const Pos& o) const { return para == o.para && offset == o.offset; }
    };
    void rebuild(float w);
    std::string linkAt(Point p);
    Pos positionAt(Point p);

    std::vector<Paragraph> paragraphs_;
    std::vector<std::unique_ptr<RichParagraph>> laid_;
    std::vector<float> tops_;
    float laidWidth_ = -1;
    float contentHeight_ = 0;
    std::string pressedLink_;
    bool selecting_ = false;
    Pos anchor_, head_;
    bool hasSelection_ = false;
    static constexpr float kInsetX = 17, kInsetY = 12;
};

/// The right-hand panel: the maths of the current filter, short lessons and experiments.
class TheoryPanel : public Widget {
public:
    explicit TheoryPanel(LabModel& model);
    SegmentedControl pageControl;
    std::function<void(const std::string&)> onCopied;
    void refresh(bool scrollToTop);
    void layout() override;
    void tick() { scroll_.tick(); }
    RichTextView& text() { return text_; }

private:
    void scheduleRefresh(bool scrollToTop);
    void linkClicked(const std::string& url);
    std::vector<Paragraph> filterPage();
    std::vector<Paragraph> lessonsPage();
    std::vector<Paragraph> lessonPage(const std::string& id);
    std::vector<Paragraph> experimentsPage();

    LabModel& model_;
    RichTextView text_;
    ScrollView scroll_{&text_};
    bool pending_ = false;
};
