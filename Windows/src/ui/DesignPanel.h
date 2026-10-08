#pragma once
#include "../app/LabModel.h"
#include "Controls.h"
#include <memory>

/// The left sidebar: choose the design method and its parameters.
class DesignPanel : public Widget {
public:
    explicit DesignPanel(LabModel& model);
    void refresh();
    void layout() override;
    void tick() { scroll_.tick(); }

private:
    /// "Selected pole pair" on the left, Delete on the right.
    class SelectionHeader : public Widget {
    public:
        SelectionHeader(Label* title, PushButton* button) : title_(title), button_(button) {
            addChild(title);
            addChild(button);
        }
        float heightForWidth(float) override { return button_->fixedHeight; }
        void layout() override {
            Size bs = button_->intrinsicSize();
            button_->setFrame({frame.w - bs.w, 0, bs.w, button_->fixedHeight});
            title_->setFrame({0, 0, std::max(10.0f, frame.w - bs.w - 8), frame.h});
        }
        bool acceptsMouse() const override { return false; }

    private:
        Label* title_;
        PushButton* button_;
    };

    std::unique_ptr<Label> section(const std::string& title);
    std::string iirHintText(const DesignSpec& s) const;
    std::string firHintText(const DesignSpec& s) const;
    void refreshPoleZero(const DesignSpec& spec, double fs);
    std::vector<Paragraph> factsText();
    void editSelected(std::optional<double> radius, std::optional<double> frequency);
    void bandChanged(int index);

    LabModel& model_;
    bool refreshing_ = false;

    StackView stack_{StackView::Orientation::vertical, 14};
    ScrollView scroll_{&stack_};
    std::unique_ptr<Label> methodSection_, factsSection_;
    SegmentedControl methodControl_;

    // IIR
    StackView iirGroup_{StackView::Orientation::vertical, 12};
    PopupButton iirBand_, family_;
    std::unique_ptr<FormRow> iirBandRow_, familyRow_;
    SliderRow order_, ripple_, stopband_;
    WrappingLabel iirHint_;

    // FIR
    StackView firGroup_{StackView::Orientation::vertical, 12};
    PopupButton firBand_, windowPopup_;
    std::unique_ptr<FormRow> firBandRow_, windowRow_;
    SliderRow taps_, beta_;
    WrappingLabel firHint_;

    // Shared cutoff rows.
    StackView cutoffGroup_{StackView::Orientation::vertical, 12};
    SliderRow cutoff_, edge1_, edge2_;

    // Pole-zero
    StackView pzGroup_{StackView::Orientation::vertical, 12};
    PopupButton presetPopup_;
    WrappingLabel presetInfo_, pzHelp_;
    Label selectedTitle_;
    PushButton deleteButton_, clearButton_;
    std::unique_ptr<SelectionHeader> selHeader_;
    SliderRow radius_, angle_;
    StackView selectedBox_{StackView::Orientation::vertical, 10};

    // Facts
    Separator separator_;
    StackView factsBox_{StackView::Orientation::vertical, 6};
    RichLabel facts_;
};
