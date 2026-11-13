#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "Engine.h"

class EarrectionProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    EarrectionProcessor();
    ~EarrectionProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Earrection"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Aus der Oberflaeche (Message-Thread)
    void pushPadEvent (int row, int col, bool on, float velocity);
    void requestClearAll();

    juce::AudioProcessorValueTreeState apvts;

    // Fuer die Anzeige: aktive bzw. wartende Spalte pro Zeile (-1 = keine)
    std::array<std::atomic<int>, earr::kRows> displayCol;
    std::array<std::atomic<int>, earr::kRows> displayPending;
    std::array<std::atomic<bool>, earr::kRows> displayHolding; // laeuft im Hold-Last-Nachlauf

    static const char* const paramIds[8];

private:
    struct PadEvent { int row = 0, col = 0; bool on = false; float vel = 1.0f; };

    struct RowPad
    {
        int col = -1;
        float vel = 1.0f;
        uint32_t order = 0;
        int pendCol = -1;
        float pendVel = 1.0f;
        double pendBeat = 0.0;
        double offBeat = -1.0;     // Hold Last: Ausschaltzeitpunkt, -1 = keiner
        bool pendRelease = false;  // losgelassen, bevor das Pad gestartet ist
    };

    void timerCallback() override;

    void padOn (int row, int col, float vel);
    void padOff (int row, int col);
    void applyOn (int row, int col, float vel);
    void applyOff (int row);
    double holdEndFrom (double fromBeat) const;
    void clearAll();
    void handleMidi (const juce::MidiMessage& m);

    earr::Engine engine;
    earr::Context ctx;

    std::array<RowPad, earr::kRows> pads;
    uint32_t orderCounter = 0;
    double beatPos = 0.0;
    double quantBeats = 0.25;
    double holdBeats = 0.0;
    bool latch = false, lastLatch = false, velocityOn = false;
    double currentSampleRate = 44100.0;

    juce::AbstractFifo fifo { 256 };
    std::array<PadEvent, 256> fifoData;
    std::atomic<bool> clearRequested { false };

    std::array<std::atomic<int>, 8> encoderDelta;

    juce::SmoothedValue<float> outGainSmoothed;

    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pQuant = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pGlide = nullptr;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pPitch = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pLatch = nullptr;
    std::atomic<float>* pVelocity = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EarrectionProcessor)
};
