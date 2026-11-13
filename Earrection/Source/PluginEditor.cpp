#include "PluginEditor.h"

using namespace juce;
namespace ec = earr::colours;

namespace
{
constexpr int kWidth = 520;
constexpr int kHeight = 684;
constexpr int kMargin = 16;
constexpr int kGridTop = 56;
constexpr int kGridSize = kWidth - 2 * kMargin; // 488
constexpr float kGap = 2.0f;

const char* const kKnobLabels[8] = { "Mix", "Quant", "Release", "Glide", "Drive", "Tone", "Pitch", "Out" };

Font monoFont (float height)
{
    return Font (FontOptions().withName (Font::getDefaultMonospacedFontName()).withHeight (height));
}

Colour groupColour (int group)
{
    return group == 0 ? ec::blue : (group == 1 ? ec::cyan : ec::pink);
}

Colour groupTextColour (int group)
{
    return group == 1 ? ec::cyanText : Colours::white;
}
} // namespace

//==============================================================================
NeonLookAndFeel::NeonLookAndFeel()
{
    setColour (TextButton::buttonColourId, ec::knobFill);
    setColour (TextButton::buttonOnColourId, ec::pink);
    setColour (TextButton::textColourOffId, ec::labelGrey);
    setColour (TextButton::textColourOnId, Colours::white);
}

void NeonLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, Slider&)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto circle = Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    g.setColour (ec::knobFill);
    g.fillEllipse (circle);

    g.setColour (ec::blue);
    g.drawEllipse (circle.reduced (0.75f), 1.5f);

    const float angle = startAngle + pos * (endAngle - startAngle);
    const Point<float> dir (std::sin (angle), -std::cos (angle));
    const auto p1 = centre + dir * (radius * 0.25f);
    const auto p2 = centre + dir * (radius - 4.0f);

    g.setColour (ec::pink);
    g.drawLine (Line<float> (p1, p2), 2.5f);
}

void NeonLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& bg,
                                            bool highlighted, bool down)
{
    auto c = bg;
    if (highlighted) c = c.brighter (0.08f);
    if (down) c = c.brighter (0.15f);
    g.setColour (c);
    g.fillRect (b.getLocalBounds());
}

Font NeonLookAndFeel::getTextButtonFont (TextButton&, int)
{
    return Font (FontOptions (12.0f, Font::bold));
}

//==============================================================================
Rectangle<float> PadGrid::cellBounds (int row, int col) const
{
    const float cell = ((float) getWidth() - kGap * (earr::kCols - 1)) / (float) earr::kCols;
    return { (float) col * (cell + kGap), (float) row * (cell + kGap), cell, cell };
}

PadGrid::Cell PadGrid::cellAt (Point<float> p) const
{
    if (! getLocalBounds().toFloat().contains (p))
        return {};
    const float step = ((float) getWidth() + kGap) / (float) earr::kCols;
    const int col = jlimit (0, earr::kCols - 1, (int) (p.x / step));
    const int row = jlimit (0, earr::kRows - 1, (int) (p.y / step));
    return { row, col };
}

bool PadGrid::isLatch() const
{
    return processor.apvts.getRawParameterValue ("latch")->load() > 0.5f;
}

bool PadGrid::refreshState()
{
    bool changed = false;
    for (int r = 0; r < earr::kRows; ++r)
    {
        const int c = processor.displayCol[(size_t) r].load();
        const int p = processor.displayPending[(size_t) r].load();
        if (c != shownCol[(size_t) r] || p != shownPending[(size_t) r])
        {
            shownCol[(size_t) r] = c;
            shownPending[(size_t) r] = p;
            changed = true;
        }
    }
    if (changed) repaint();
    return changed;
}

void PadGrid::paint (Graphics& g)
{
    g.setFont (monoFont (12.0f));

    for (int r = 0; r < earr::kRows; ++r)
    {
        const int group = earr::rowGroup (r);
        for (int c = 0; c < earr::kCols; ++c)
        {
            const auto b = cellBounds (r, c);
            const bool on = shownCol[(size_t) r] == c;
            const bool pending = shownPending[(size_t) r] == c;

            g.setColour (on ? Colours::white : groupColour (group));
            g.fillRect (b);

            if (pending && ! on)
            {
                g.setColour (Colours::white);
                g.drawRect (b.reduced (1.0f), 2.0f);
            }

            g.setColour (on ? ec::background : groupTextColour (group));
            g.drawText (earr::cellLabel (r, c), b, Justification::centred, false);
        }
    }
}

void PadGrid::mouseDown (const MouseEvent& e)
{
    const auto cell = cellAt (e.position);
    if (! cell.valid()) return;
    processor.pushPadEvent (cell.row, cell.col, true, 1.0f);
    lastCell = cell;
}

void PadGrid::mouseDrag (const MouseEvent& e)
{
    if (isLatch() || ! lastCell.valid()) return;

    const auto cell = cellAt (e.position);
    if (! cell.valid() || (cell.row == lastCell.row && cell.col == lastCell.col))
        return;

    if (cell.row != lastCell.row && ! e.mods.isShiftDown())
        processor.pushPadEvent (lastCell.row, lastCell.col, false, 1.0f);

    processor.pushPadEvent (cell.row, cell.col, true, 1.0f);
    lastCell = cell;
}

void PadGrid::mouseUp (const MouseEvent& e)
{
    // Shift-Klick: Pad bleibt gehalten, bis es normal angeklickt wird
    if (! isLatch() && lastCell.valid() && ! e.mods.isShiftDown())
        processor.pushPadEvent (lastCell.row, lastCell.col, false, 1.0f);
    lastCell = {};
}

//==============================================================================
EarrectionEditor::EarrectionEditor (EarrectionProcessor& p)
    : AudioProcessorEditor (&p), processor (p), grid (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (grid);

    for (int i = 0; i < 8; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.setSliderStyle (Slider::RotaryVerticalDrag);
        k.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        k.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        k.setMouseDragSensitivity (180);
        addAndMakeVisible (k);
        knobAttachments[(size_t) i] = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, EarrectionProcessor::paramIds[i], k);

        k.onValueChange = [this, i]
        {
            if (auto* param = processor.apvts.getParameter (EarrectionProcessor::paramIds[i]))
            {
                status = String (kKnobLabels[i]).toUpperCase() + "  " + param->getCurrentValueAsText();
                statusHoldUntil = Time::getMillisecondCounter() + 1500;
                repaint (0, 0, getWidth(), kGridTop);
            }
        };
    }

    for (auto* b : { &latchButton, &velocityButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    latchAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "latch", latchButton);
    velocityAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "velocity", velocityButton);

    clearButton.onClick = [this] { processor.requestClearAll(); };
    addAndMakeVisible (clearButton);

    setSize (kWidth, kHeight);
    startTimerHz (30);
}

EarrectionEditor::~EarrectionEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

String EarrectionEditor::activeText() const
{
    StringArray parts;
    for (int r = 0; r < earr::kRows; ++r)
    {
        const int c = processor.displayCol[(size_t) r].load();
        if (c >= 0)
            parts.add (String (earr::rowName (r)).toUpperCase() + " " + earr::cellLabel (r, c));
    }
    return parts.isEmpty() ? String ("-") : parts.joinIntoString ("  +  ");
}

void EarrectionEditor::timerCallback()
{
    grid.refreshState();

    const auto text = Time::getMillisecondCounter() < statusHoldUntil ? status : activeText();
    if (text != lastStatus)
    {
        lastStatus = text;
        repaint (0, 0, getWidth(), kGridTop);
    }
}

void EarrectionEditor::paint (Graphics& g)
{
    g.fillAll (ec::background);

    // Kopfzeile
    auto header = Rectangle<int> (kMargin, kMargin, kGridSize, kGridTop - kMargin - 8);
    g.setColour (Colours::white);
    g.setFont (Font (FontOptions (16.0f, Font::bold)));
    const String title ("EARRECTION");
    {
        // Titel mit Buchstabenabstand
        float x = (float) header.getX();
        for (auto ch : title)
        {
            const String s = String::charToString (ch);
            g.drawText (s, Rectangle<float> (x, (float) header.getY(), 20.0f, (float) header.getHeight()), Justification::centredLeft, false);
            x += GlyphArrangement::getStringWidth (g.getCurrentFont(), s) + 3.0f;
        }
    }

    g.setColour (ec::cyan);
    g.setFont (monoFont (12.0f));
    g.drawFittedText (lastStatus, header.withTrimmedLeft (170), Justification::centredRight, 1);

    // Regler-Beschriftungen
    g.setColour (ec::labelGrey);
    g.setFont (Font (FontOptions (11.0f)));
    for (int i = 0; i < 8; ++i)
    {
        const auto kb = knobs[(size_t) i].getBounds();
        g.drawText (kKnobLabels[i], Rectangle<int> (kb.getX() - 6, kb.getBottom() + 2, kb.getWidth() + 12, 14),
                    Justification::centred, false);
    }

    // Hinweis unten rechts
    g.setColour (ec::labelGrey.withAlpha (0.7f));
    g.setFont (Font (FontOptions (11.0f)));
    g.drawText ("Push: User Mode  |  Shift-Klick = halten",
                Rectangle<int> (kMargin, kHeight - kMargin - 28, kGridSize, 28), Justification::centredRight, false);
}

void EarrectionEditor::resized()
{
    grid.setBounds (kMargin, kGridTop, kGridSize, kGridSize);

    const int knobTop = kGridTop + kGridSize + 16;
    const int colW = kGridSize / 8;
    for (int i = 0; i < 8; ++i)
    {
        const int cx = kMargin + i * colW + colW / 2;
        knobs[(size_t) i].setBounds (cx - 22, knobTop, 44, 44);
    }

    const int buttonsTop = kHeight - kMargin - 28;
    latchButton.setBounds (kMargin, buttonsTop, 84, 28);
    velocityButton.setBounds (kMargin + 90, buttonsTop, 96, 28);
    clearButton.setBounds (kMargin + 192, buttonsTop, 72, 28);
}
