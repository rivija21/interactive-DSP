#pragma once
#include "../app/LabModel.h"
#include "Controls.h"

/// Source selection, listening controls, A/B switch, sample rate and meters.
class TopBar : public Widget {
public:
    explicit TopBar(LabModel& model);
    void refresh();
    /// Called every frame for live readouts.
    void tick();
    void layout() override;
    void draw(Canvas& c) override;
    void drawOverChildren(Canvas& c) override;
    LevelMeter meter;

private:
    LabModel& model_;
    bool refreshing_ = false;
    IconButton playButton_;
    PopupButton sourcePopup_;
    SliderRow frequencyRow_;
    SliderRow micGainRow_;
    Label sweepLabel_;
    Label fileLabel_;
    PushButton openButton_;
    StackView sourceExtras_{StackView::Orientation::horizontal, 10};
    PopupButton ratePopup_;
    SegmentedControl abControl_;
    PushButton listenButton_;
    Slider volumeSlider_;
    StackView left_{StackView::Orientation::horizontal, 10};
    StackView right_{StackView::Orientation::horizontal, 12};
};

/// A one-line message strip under the top bar (tips, warnings, instability).
class BannerView : public Widget {
public:
    explicit BannerView(LabModel& model);
    void refresh();
    void layout() override;
    void draw(Canvas& c) override;
    /// The height the banner is animating towards (0 or 34).
    float targetHeight = 0;

private:
    LabModel& model_;
    Label label_;
    PushButton actionButton_;
    IconButton closeButton_;
    Color color_;
};
