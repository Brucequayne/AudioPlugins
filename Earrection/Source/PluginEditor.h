#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace earr::colours
{
const juce::Colour background { 0xff090a1a };
const juce::Colour blue       { 0xff3a3dff };
const juce::Colour cyan       { 0xff00e5ff };
const juce::Colour pink       { 0xffff2e9a };
const juce::Colour knobFill   { 0xff2e3552 };
const juce::Colour labelGrey  { 0xff8a8fb8 };
const juce::Colour cyanText   { 0xff04222a };
}

//==============================================================================
class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& bg,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

//==============================================================================
class PadGrid : public juce::Component
{
public:
    explicit PadGrid (EarrectionProcessor& p) : processor (p) {}

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool refreshState(); // true, wenn sich etwas geaendert hat

private:
    struct Cell { int row = -1, col = -1; bool valid() const { return row >= 0; } };
    Cell cellAt (juce::Point<float>) const;
    juce::Rectangle<float> cellBounds (int row, int col) const;
    bool isLatch() const;

    EarrectionProcessor& processor;
    std::array<int, earr::kRows> shownCol {}, shownPending {};
    Cell lastCell;
};

//==============================================================================
class EarrectionEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit EarrectionEditor (EarrectionProcessor&);
    ~EarrectionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::String activeText() const;

    EarrectionProcessor& processor;
    NeonLookAndFeel lnf;
    PadGrid grid;

    std::array<juce::Slider, 8> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> knobAttachments;

    juce::TextButton latchButton { "LATCH" }, velocityButton { "VELOCITY" }, clearButton { "CLEAR" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> latchAttachment, velocityAttachment;

    juce::String status, lastStatus;
    juce::uint32 statusHoldUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EarrectionEditor)
};
