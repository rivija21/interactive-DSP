#pragma once
#include "../app/LabModel.h"
#include "Controls.h"
#include "DesignPanel.h"
#include "LiveViews.h"
#include "ResponseView.h"
#include "TheoryPanel.h"
#include "TopBar.h"
#include "ZPlaneView.h"

/// The little "Copied ..." message that fades in over the live panel.
class Toast : public Widget {
public:
    void show(const std::string& message);
    void tick();
    Size intrinsicSize() override;
    void draw(Canvas& c) override;
    bool acceptsMouse() const override { return false; }

private:
    std::string text_;
    double shownAt_ = -100;
    float alpha_ = 0;
};

/// A container whose children all fill it (the stacked live views).
class FillContainer : public Widget {
public:
    void layout() override {
        for (auto* c : children) c->setFrame(bounds());
    }
    bool acceptsMouse() const override { return false; }
};

/// Lays out the whole window and drives the live displays.
class MainView : public Widget {
public:
    explicit MainView(LabModel& model);
    static const std::vector<double> scopeWindows;

    LabModel& model;
    TopBar topBar;
    BannerView banner;
    DesignPanel design;
    ZPlaneView zPlane;
    ResponseView response;
    SpectrumView spectrum;
    SpectrogramView spectrogram;
    ScopeView scope;
    TheoryPanel theory;

    void layout() override;
    void draw(Canvas& c) override;
    void tick();
    void showToast(const std::string& message);
    void setTheoryVisible(bool visible);
    bool theoryVisible() const { return !theoryPanel_.hidden(); }
    /// Narrowest window content that satisfies the layout's minimum sizes.
    float minimumWidth() const;

    void exportZ();
    void exportResponse();
    void exportLive();

private:
    void buildHeaders();
    void modelChanged(ModelChange change);
    void refreshHeaders();

    Panel designPanel_{"Design"};
    Panel zPanel_{"Z-plane"};
    Panel responsePanel_{"Response"};
    Panel livePanel_{"Live"};
    Panel theoryPanel_{"Theory"};
    FillContainer liveBody_;

    SegmentedControl toolControl_{{"Move", "+ Zero", "+ Pole"}};
    Checkbox mapButton_{"|H| map"};
    IconButton resetButton_{Icon::zoomReset, "Reset zoom (scroll or pinch to zoom, drag empty space to pan)"};
    IconButton zExport_{Icon::share, "Export this plot as a PNG figure"};
    SegmentedControl responseControl_;
    IconButton rExport_{Icon::share, "Export this plot as a PNG figure"};
    SegmentedControl liveControl_;
    SegmentedControl axisControl_{{"Log f", "Linear f"}};
    Checkbox predictionBox_{"Predicted"};
    SegmentedControl spectrogramSource_{{"Input", "Output"}};
    PopupButton scopeWindow_;
    Checkbox holdButton_{"Hold"};
    IconButton lExport_{Icon::share, "Export this view as a PNG figure"};
    Toast toast_;

    double lastMarkerFrequency_ = 0;
    float bannerHeight_ = 0;
};
