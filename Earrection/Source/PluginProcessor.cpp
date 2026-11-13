#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

const char* const EarrectionProcessor::paramIds[8] = { "mix", "quant", "release", "glide", "drive", "hold", "pitch", "output" };

namespace
{
const double kQuantBeats[] = { 0.0, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };
const double kHoldBeats[] = { 0.0, 1.0, 2.0, 4.0, 8.0, 16.0 };

double releaseMs (float v) { return 5.0 * std::pow (800.0, (double) v); }
double glideMs (float v)   { return 2.0 * std::pow (250.0, (double) v); }

String msText (double ms)
{
    return ms < 1000.0 ? String (roundToInt (ms)) + " ms" : String (ms / 1000.0, 1) + " s";
}

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "quant", 1 }, "Quantize",
        StringArray { "Aus", "1/32", "1/16", "1/8", "1/4", "1/2", "1 Takt" }, 2));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release",
        NormalisableRange<float> (0.0f, 1.0f), 0.1f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return msText (releaseMs (v)); })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Glide",
        NormalisableRange<float> (0.0f, 1.0f), 0.1f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return msText (glideMs (v)); })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive",
        NormalisableRange<float> (0.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "hold", 1 }, "Hold Last",
        StringArray { "Aus", "1/4 Takt", "1/2 Takt", "1 Takt", "2 Takte", "4 Takte" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "pitch", 1 }, "Pitch",
        NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            const int st = roundToInt (v);
            return (st > 0 ? "+" : "") + String (st) + " st";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-24.0f, 6.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return String (v, 1) + " dB";
        })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "latch", 1 }, "Latch", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "velocity", 1 }, "Velocity", false));

    return layout;
}
} // namespace

//==============================================================================
EarrectionProcessor::EarrectionProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EarrectionState", createLayout())
{
    for (auto& d : displayCol) d.store (-1);
    for (auto& d : displayPending) d.store (-1);
    for (auto& d : displayHolding) d.store (false);
    for (auto& e : encoderDelta) e.store (0);

    pMix      = apvts.getRawParameterValue ("mix");
    pQuant    = apvts.getRawParameterValue ("quant");
    pRelease  = apvts.getRawParameterValue ("release");
    pGlide    = apvts.getRawParameterValue ("glide");
    pDrive    = apvts.getRawParameterValue ("drive");
    pHold     = apvts.getRawParameterValue ("hold");
    pPitch    = apvts.getRawParameterValue ("pitch");
    pOutput   = apvts.getRawParameterValue ("output");
    pLatch    = apvts.getRawParameterValue ("latch");
    pVelocity = apvts.getRawParameterValue ("velocity");

    startTimerHz (30);
}

EarrectionProcessor::~EarrectionProcessor()
{
    stopTimer();
}

bool EarrectionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void EarrectionProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    ctx.sampleRate = sampleRate;
    for (auto& p : pads) p = {};
    outGainSmoothed.reset (sampleRate, 0.05);
    outGainSmoothed.setCurrentAndTargetValue (Decibels::decibelsToGain (pOutput->load()));
}

//==============================================================================
void EarrectionProcessor::pushPadEvent (int row, int col, bool on, float velocity)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        fifoData[(size_t) scope.startIndex1] = { row, col, on, velocity };
    else if (scope.blockSize2 > 0)
        fifoData[(size_t) scope.startIndex2] = { row, col, on, velocity };
}

void EarrectionProcessor::requestClearAll()
{
    clearRequested.store (true);
}

void EarrectionProcessor::padOn (int row, int col, float vel)
{
    auto& p = pads[(size_t) row];
    const float v = velocityOn ? vel : 1.0f;
    p.offBeat = -1.0;
    p.pendRelease = false;

    if (latch)
    {
        const int current = p.pendCol >= 0 ? p.pendCol : p.col;
        if (current == col)
        {
            p.pendCol = -1;
            if (p.col >= 0) applyOff (row);
            return;
        }
    }

    if (quantBeats <= 0.0)
    {
        p.pendCol = -1;
        applyOn (row, col, v);
        return;
    }

    p.pendCol = col;
    p.pendVel = v;
    p.pendBeat = std::ceil (beatPos / quantBeats - 1.0e-6) * quantBeats;
}

void EarrectionProcessor::padOff (int row, int col)
{
    if (latch)
        return;

    auto& p = pads[(size_t) row];

    if (holdBeats > 0.0)
    {
        // Hold Last: Pad laeuft noch die eingestellte Zeit weiter
        if (p.pendCol == col) { p.pendRelease = true; return; }
        if (p.col == col) p.offBeat = holdEndFrom (beatPos);
        return;
    }

    if (p.pendCol == col) p.pendCol = -1;
    if (p.col == col) applyOff (row);
}

double EarrectionProcessor::holdEndFrom (double fromBeat) const
{
    double end = fromBeat + holdBeats;
    if (quantBeats > 0.0)
        end = std::ceil (end / quantBeats - 1.0e-6) * quantBeats;
    return end;
}

void EarrectionProcessor::applyOn (int row, int col, float vel)
{
    auto& p = pads[(size_t) row];

    if (p.col < 0)
    {
        int count = 0, oldest = -1;
        uint32_t oldestOrder = 0xffffffffu;
        for (int r = 0; r < earr::kRows; ++r)
        {
            if (pads[(size_t) r].col >= 0)
            {
                ++count;
                if (pads[(size_t) r].order < oldestOrder) { oldestOrder = pads[(size_t) r].order; oldest = r; }
            }
        }
        if (count >= earr::kMaxActive && oldest >= 0)
            applyOff (oldest);
    }

    p.col = col;
    p.vel = vel;
    p.order = ++orderCounter;
    engine.trigger (row, col, vel, ctx);
}

void EarrectionProcessor::applyOff (int row)
{
    pads[(size_t) row].col = -1;
    pads[(size_t) row].offBeat = -1.0;
    pads[(size_t) row].pendRelease = false;
    engine.release (row);
}

void EarrectionProcessor::clearAll()
{
    for (int r = 0; r < earr::kRows; ++r)
    {
        pads[(size_t) r].pendCol = -1;
        if (pads[(size_t) r].col >= 0) applyOff (r);
    }
}

void EarrectionProcessor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOnOrOff())
    {
        const int note = m.getNoteNumber();
        if (note < 36 || note > 99) return;

        // Push User Mode: Note 36 = unten links, aufsteigend nach rechts und oben
        const int idx = note - 36;
        const int row = earr::kRows - 1 - idx / earr::kCols;
        const int col = idx % earr::kCols;

        if (m.isNoteOn())
            padOn (row, col, m.getFloatVelocity());
        else
            padOff (row, col);
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const int val = m.getControllerValue();

        if (cc >= 71 && cc <= 78) // Push-Encoder (relativ)
        {
            const int delta = val < 64 ? val : val - 128;
            encoderDelta[(size_t) (cc - 71)].fetch_add (delta);
        }
        else if (cc == 123)
        {
            clearAll();
        }
    }
}

void EarrectionProcessor::timerCallback()
{
    static const float sensitivity[8] = { 0.006f, 0.03f, 0.006f, 0.006f, 0.006f, 0.04f, 0.012f, 0.006f };

    for (int i = 0; i < 8; ++i)
    {
        const int d = encoderDelta[(size_t) i].exchange (0);
        if (d == 0) continue;

        if (auto* param = apvts.getParameter (paramIds[i]))
        {
            const float nv = jlimit (0.0f, 1.0f, param->getValue() + (float) d * sensitivity[i]);
            param->beginChangeGesture();
            param->setValueNotifyingHost (nv);
            param->endChangeGesture();
        }
    }
}

//==============================================================================
void EarrectionProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    // Tempo und Position vom Host
    double bpm = 120.0;
    bool playing = false;
    juce::Optional<double> ppq;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = jlimit (20.0, 400.0, *b);
            playing = pos->getIsPlaying();
            ppq = pos->getPpqPosition();
        }
    }
    if (playing && ppq.hasValue())
        beatPos = *ppq;

    const double samplesPerBeat = currentSampleRate * 60.0 / bpm;
    const double beatsPerSample = 1.0 / samplesPerBeat;

    // Parameter
    const int quantIndex = jlimit (0, 6, (int) pQuant->load());
    quantBeats = kQuantBeats[quantIndex];
    holdBeats = kHoldBeats[jlimit (0, 5, (int) pHold->load())];
    latch = pLatch->load() > 0.5f;
    velocityOn = pVelocity->load() > 0.5f;
    if (lastLatch && ! latch)
        clearAll();
    lastLatch = latch;

    ctx.samplesPerBeat = samplesPerBeat;
    ctx.pitchRatio = std::pow (2.0, (double) pPitch->load() / 12.0);
    ctx.glideSamples = glideMs (pGlide->load()) * 0.001 * currentSampleRate;
    ctx.beatPos = beatPos;

    earr::Engine::Globals g;
    g.mix = pMix->load();
    g.drive = pDrive->load();
    g.attackSamples = std::max (0.001 * currentSampleRate, ctx.glideSamples);
    g.releaseSamples = releaseMs (pRelease->load()) * 0.001 * currentSampleRate;
    outGainSmoothed.setTargetValue (Decibels::decibelsToGain (pOutput->load()));

    // Ereignisse aus der Oberflaeche
    if (clearRequested.exchange (false))
        clearAll();

    {
        const auto scope = fifo.read (fifo.getNumReady());
        auto apply = [this] (int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto& e = fifoData[(size_t) (start + i)];
                if (e.on) padOn (e.row, e.col, e.vel);
                else      padOff (e.row, e.col);
            }
        };
        apply (scope.startIndex1, scope.blockSize1);
        apply (scope.startIndex2, scope.blockSize2);
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    auto midiIt = midi.cbegin();
    const auto midiEnd = midi.cend();

    for (int i = 0; i < n; ++i)
    {
        while (midiIt != midiEnd && (*midiIt).samplePosition <= i)
        {
            handleMidi ((*midiIt).getMessage());
            ++midiIt;
        }

        // wartende Pads auf dem naechsten Raster starten
        for (int r = 0; r < earr::kRows; ++r)
        {
            auto& p = pads[(size_t) r];
            if (p.pendCol < 0) continue;
            const bool due = beatPos >= p.pendBeat - 1.0e-9;
            const bool jumped = (p.pendBeat - beatPos) > quantBeats + 1.0e-6; // Transport-Sprung
            if (due || jumped)
            {
                const int c = p.pendCol;
                const bool releaseLater = p.pendRelease;
                p.pendCol = -1;
                applyOn (r, c, p.pendVel);
                if (releaseLater)
                    p.offBeat = holdEndFrom (beatPos);
            }
        }

        // Hold Last abgelaufen?
        for (int r = 0; r < earr::kRows; ++r)
        {
            auto& p = pads[(size_t) r];
            if (p.col < 0 || p.offBeat < 0.0) continue;
            const bool due = beatPos >= p.offBeat - 1.0e-9;
            const bool jumped = (p.offBeat - beatPos) > holdBeats + quantBeats + 1.0e-6;
            if (due || jumped)
                applyOff (r);
        }

        float l = left[i];
        float rr = right != nullptr ? right[i] : l;

        g.outGain = outGainSmoothed.getNextValue();
        engine.processSample (l, rr, ctx, g);

        left[i] = l;
        if (right != nullptr) right[i] = rr;

        beatPos += beatsPerSample;
        ctx.beatPos = beatPos;
    }

    while (midiIt != midiEnd)
    {
        handleMidi ((*midiIt).getMessage());
        ++midiIt;
    }

    midi.clear();

    for (int r = 0; r < earr::kRows; ++r)
    {
        displayCol[(size_t) r].store (pads[(size_t) r].col);
        displayPending[(size_t) r].store (pads[(size_t) r].pendCol);
        displayHolding[(size_t) r].store (pads[(size_t) r].offBeat >= 0.0);
    }
}

//==============================================================================
AudioProcessorEditor* EarrectionProcessor::createEditor()
{
    return new EarrectionEditor (*this);
}

void EarrectionProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void EarrectionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EarrectionProcessor();
}
