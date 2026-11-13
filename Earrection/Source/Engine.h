#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

// Earrection – Audio-Engine
// 8 Zeilen (Effektfamilien) x 8 Spalten (Varianten). Pro Zeile ist hoechstens
// eine Variante aktiv, insgesamt bis zu 4 Zeilen gleichzeitig. Die Zeilen
// werden von oben nach unten hintereinander geschaltet.

namespace earr
{
constexpr int kRows = 8;
constexpr int kCols = 8;
constexpr int kMaxActive = 4;

inline const char* rowName (int row)
{
    static const char* names[kRows] = { "Stutter", "Repeat", "Reverse", "Tempo", "Echo", "Gate", "Filter", "Klang" };
    return names[juce::jlimit (0, kRows - 1, row)];
}

// 0 = Puffer (blau), 1 = Rhythmus (cyan), 2 = Klang (pink)
inline int rowGroup (int row) { return row < 4 ? 0 : (row < 6 ? 1 : 2); }

inline juce::String cellLabel (int row, int col)
{
    static const char* labels[kRows][kCols] = {
        { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64", "1/128" },
        { "2Tk", "1Tk", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" },
        { "1/16", "1/8", "1/4", "1/2", "1Tk", "2Tk", "\xc2\xbd\xc3\x97", "2\xc3\x97" },
        { "Halb", "2\xc3\x97", "T2", "T1", "T\xc2\xbd", "T\xc2\xbc", "Spin", "Stop" },
        { "1/2", "1/4.", "1/4", "1/8.", "1/8", "1/8T", "1/16", "\xe2\x88\x9e" },
        { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "Swg", "Rnd" },
        { "LP1", "LP2", "LP3", "LP4", "HP1", "HP2", "HP3", "HP4" },
        { "Cr1", "Cr2", "Cr3", "Dn1", "Dn2", "Grn", "Frz", "Chs" }
    };
    return juce::String::fromUTF8 (labels[juce::jlimit (0, kRows - 1, row)][juce::jlimit (0, kCols - 1, col)]);
}

struct Context
{
    double beatPos = 0.0;          // Position in Vierteln (Beats)
    double samplesPerBeat = 22050.0;
    double pitchRatio = 1.0;       // aus dem Pitch-Regler
    double glideSamples = 128.0;   // Uebergangszeit aus dem Glide-Regler
    double sampleRate = 44100.0;
};

inline uint32_t hash32 (int64_t x)
{
    auto v = (uint64_t) x;
    v ^= v >> 33; v *= 0xff51afd7ed558ccdULL;
    v ^= v >> 33; v *= 0xc4ceb9fe1a85ec53ULL;
    v ^= v >> 33;
    return (uint32_t) v;
}

//==============================================================================
// Ringpuffer, der fortlaufend das Eingangssignal einer Zeile aufnimmt
class History
{
public:
    void prepare (int numSamples)
    {
        size = std::max (1024, numSamples);
        bufL.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);
        written = 0;
    }

    void push (float a, float b)
    {
        const auto i = (size_t) (written % size);
        bufL[i] = a;
        bufR[i] = b;
        ++written;
    }

    int64_t now() const { return written; }
    int getSize() const { return size; }

    void read (double p, float& a, float& b) const
    {
        if (written < 2) { a = b = 0.0f; return; }

        const double hi = (double) (written - 1);
        const double lo = std::max (0.0, (double) (written - (int64_t) size + 2));
        p = juce::jlimit (lo, hi, p);

        const auto i0 = (int64_t) std::floor (p);
        const auto f = (float) (p - (double) i0);
        const auto k0 = (size_t) (i0 % size);
        const auto k1 = (size_t) ((i0 + 1) % size);
        a = bufL[k0] + (bufL[k1] - bufL[k0]) * f;
        b = bufR[k0] + (bufR[k1] - bufR[k0]) * f;
    }

    // kopiert die letzten n Samples in Zielpuffer (aelteste zuerst)
    void copyLast (int n, std::vector<float>& dl, std::vector<float>& dr) const
    {
        n = std::min (n, (int) dl.size());
        const int64_t start = written - n;
        for (int k = 0; k < n; ++k)
        {
            const int64_t idx = start + k;
            if (idx < 0 || idx < written - size) { dl[(size_t) k] = dr[(size_t) k] = 0.0f; continue; }
            const auto i = (size_t) (idx % size);
            dl[(size_t) k] = bufL[i];
            dr[(size_t) k] = bufR[i];
        }
    }

private:
    std::vector<float> bufL, bufR;
    int size = 1024;
    int64_t written = 0;
};

//==============================================================================
class Row
{
public:
    virtual ~Row() = default;
    virtual void prepare (double sampleRate) = 0;
    virtual void trigger (int col, bool fromIdle, const Context&) = 0;
    virtual void release() {}
    virtual void stop() {}
    // engaged = Zeile ist aktiv oder klingt noch aus.
    // l/r enthalten danach das voll bearbeitete Signal.
    virtual void process (float& l, float& r, bool engaged, const Context&) = 0;
};

//==============================================================================
// Zeilen 0-3: Stutter, Repeat, Reverse, Tempo – alles Puffer-Effekte
class LoopRow : public Row
{
public:
    explicit LoopRow (int rowIndex) : row (rowIndex) {}

    void prepare (double sr) override
    {
        sampleRate = sr;
        hist.prepare ((int) (sr * 10.0));
        if (row <= 1) // feste Loops brauchen eigene Kopien
            for (int i = 0; i < 2; ++i)
            {
                loopL[i].assign ((size_t) hist.getSize(), 0.0f);
                loopR[i].assign ((size_t) hist.getSize(), 0.0f);
            }
        cur = {};
        old = {};
        oldFade = 0.0f;
    }

    void trigger (int col, bool, const Context& c) override
    {
        if (cur.on)
        {
            old = cur;
            oldFade = 1.0f;
            fadeStep = 1.0f / (float) std::max (64.0, c.glideSamples);
        }
        const int nextBuf = (cur.buf == 0) ? 1 : 0;
        cur = makeVoice (col, c, nextBuf);
    }

    void stop() override
    {
        cur.on = false;
        old.on = false;
        oldFade = 0.0f;
    }

    void process (float& l, float& r, bool engaged, const Context& c) override
    {
        hist.push (l, r);
        if (! engaged)
            return;

        float al = l, ar = r;
        render (cur, c, al, ar);

        if (old.on && oldFade > 0.0f)
        {
            float bl = l, br = r;
            render (old, c, bl, br);
            al = al * (1.0f - oldFade) + bl * oldFade;
            ar = ar * (1.0f - oldFade) + br * oldFade;
            oldFade -= fadeStep;
            if (oldFade <= 0.0f) { oldFade = 0.0f; old.on = false; }
        }

        l = al;
        r = ar;
    }

private:
    enum class Mode { Loop, RefreshFwd, RefreshRev, TapeStop, Spin };

    struct Voice
    {
        bool on = false;
        Mode mode = Mode::Loop;
        double start = 0.0, len = 1.0, pos = 0.0, rate = 1.0;
        double t = 0.0, dur = 1.0;
        bool gate = false, usePitch = true;
        int buf = -1; // >= 0: liest aus eigener Loop-Kopie
    };

    Voice makeVoice (int col, const Context& c, int bufIndex)
    {
        Voice v;
        v.on = true;
        const double spb = c.samplesPerBeat;
        const double maxLen = (double) hist.getSize() - 16.0;
        auto beatsToLen = [&] (double beats) { return juce::jlimit (32.0, maxLen, beats * spb); };

        switch (row)
        {
            case 0:
            {
                static const double L[kCols] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625, 0.03125 };
                v.mode = Mode::Loop;
                v.len = beatsToLen (L[col]);
                v.gate = true;
                break;
            }
            case 1:
            {
                static const double L[kCols] = { 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
                v.mode = Mode::Loop;
                v.len = beatsToLen (L[col]);
                break;
            }
            case 2:
            {
                static const double L[kCols] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 1.0, 1.0 };
                static const double R[kCols] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.5, 2.0 };
                v.mode = Mode::RefreshRev;
                v.len = beatsToLen (L[col]);
                v.rate = R[col];
                break;
            }
            default:
            {
                switch (col)
                {
                    case 0: v.mode = Mode::RefreshFwd; v.len = beatsToLen (2.0); v.rate = 0.5; v.usePitch = false; break;
                    case 1: v.mode = Mode::RefreshFwd; v.len = beatsToLen (1.0); v.rate = 2.0; v.usePitch = false; break;
                    case 6: v.mode = Mode::Spin; v.dur = std::max (32.0, 1.0 * spb); break;
                    case 7: v.mode = Mode::TapeStop; v.dur = std::max (32.0, 0.0625 * spb); break;
                    default:
                    {
                        static const double D[4] = { 2.0, 1.0, 0.5, 0.25 };
                        v.mode = Mode::TapeStop;
                        v.dur = std::max (32.0, D[col - 2] * spb);
                        break;
                    }
                }
                break;
            }
        }

        if (v.mode == Mode::TapeStop || v.mode == Mode::Spin)
        {
            v.start = (double) hist.now() - 1.0;
            v.usePitch = false;
        }
        else
        {
            v.start = (double) hist.now() - v.len;
        }

        if (v.mode == Mode::Loop && row <= 1)
        {
            v.buf = bufIndex;
            hist.copyLast ((int) std::ceil (v.len) + 1, loopL[bufIndex], loopR[bufIndex]);
        }

        return v;
    }

    void readLoop (const Voice& v, double p, float& a, float& b) const
    {
        const auto& bl = loopL[v.buf];
        const auto& br = loopR[v.buf];
        const int n = (int) std::ceil (v.len) + 1;
        p = juce::jlimit (0.0, (double) (n - 1), p);
        const int i0 = (int) p;
        const int i1 = std::min (i0 + 1, n - 1);
        const auto f = (float) (p - i0);
        a = bl[(size_t) i0] + (bl[(size_t) i1] - bl[(size_t) i0]) * f;
        b = br[(size_t) i0] + (br[(size_t) i1] - br[(size_t) i0]) * f;
    }

    void render (Voice& v, const Context& c, float& l, float& r)
    {
        if (! v.on)
            return; // Eingang unveraendert

        float a = 0.0f, b = 0.0f;

        if (v.mode == Mode::TapeStop || v.mode == Mode::Spin)
        {
            const double u = std::min (1.0, v.t / v.dur);
            double rate;
            float gain;
            if (v.mode == Mode::TapeStop)
            {
                rate = std::max (0.0, 1.0 - u);
                gain = (float) juce::jlimit (0.0, 1.0, (1.0 - u) * 8.0);
            }
            else
            {
                rate = -3.0 * (1.0 - u) * (1.0 - u);
                gain = (float) juce::jlimit (0.0, 1.0, (1.0 - u) * 4.0);
            }
            hist.read (v.start + v.pos, a, b);
            v.pos += rate;
            v.t += 1.0;
            l = a * gain;
            r = b * gain;
            return;
        }

        const double rate = v.rate * (v.usePitch ? c.pitchRatio : 1.0);
        const double p = v.pos;

        if (v.mode == Mode::RefreshRev)
            hist.read (v.start + v.len - 1.0 - p, a, b);
        else if (v.buf >= 0)
            readLoop (v, p, a, b);
        else
            hist.read (v.start + p, a, b);

        const double edge = std::min (64.0, v.len * 0.25);
        double env = juce::jlimit (0.0, 1.0, std::min (p / edge, (v.len - p) / edge));
        if (v.gate)
            env *= juce::jlimit (0.0, 1.0, (v.len * 0.7 - p) / edge);

        v.pos += rate;
        if (v.pos >= v.len)
        {
            v.pos = std::fmod (v.pos, v.len);
            if (v.mode == Mode::RefreshFwd || v.mode == Mode::RefreshRev)
                v.start = (double) hist.now() - v.len;
        }

        l = a * (float) env;
        r = b * (float) env;
    }

    const int row;
    double sampleRate = 44100.0;
    History hist;
    std::vector<float> loopL[2], loopR[2];
    Voice cur, old;
    float oldFade = 0.0f, fadeStep = 0.01f;
};

//==============================================================================
// Zeile 4: tempo-synchrones Echo
class EchoRow : public Row
{
public:
    void prepare (double sr) override
    {
        sampleRate = sr;
        size = (int) (sr * 4.5);
        bufL.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);
        writePos = 0;
        lpCoef = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 6000.0f / (float) sr);
    }

    void trigger (int col, bool fromIdle, const Context& c) override
    {
        static const double T[kCols] = { 2.0, 1.5, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 0.25 };
        if (fromIdle)
        {
            std::fill (bufL.begin(), bufL.end(), 0.0f);
            std::fill (bufR.begin(), bufR.end(), 0.0f);
            lpL = lpR = 0.0f;
            feedGain = 0.0f;
        }
        targetDelay = juce::jlimit (16.0, (double) size - 4.0, T[col] * c.samplesPerBeat);
        if (fromIdle) delay = targetDelay;
        infinite = (col == 7);
        feedTarget = 1.0f;
        infFeedLeft = infinite ? (int) targetDelay : 0;
    }

    void release() override { feedTarget = 0.0f; }

    void process (float& l, float& r, bool engaged, const Context&) override
    {
        if (! engaged)
            return;

        delay += (targetDelay - delay) * 0.0005;

        double rp = (double) writePos - delay;
        while (rp < 0.0) rp += size;
        const int i0 = (int) rp;
        const int i1 = (i0 + 1) % size;
        const auto f = (float) (rp - i0);
        const float dl = bufL[(size_t) i0] + (bufL[(size_t) i1] - bufL[(size_t) i0]) * f;
        const float dr = bufR[(size_t) i0] + (bufR[(size_t) i1] - bufR[(size_t) i0]) * f;

        float target = feedTarget;
        if (infinite)
            target = (infFeedLeft > 0 && feedTarget > 0.0f) ? 1.0f : 0.0f;
        if (infFeedLeft > 0) --infFeedLeft;
        feedGain += (target - feedGain) * 0.003f;

        lpL += (dl - lpL) * lpCoef;
        lpR += (dr - lpR) * lpCoef;

        const float fb = infinite ? 1.0f : 0.5f;
        float wl = l * feedGain + lpL * fb;
        float wr = r * feedGain + lpR * fb;
        if (infinite) { wl = std::tanh (wl); wr = std::tanh (wr); }
        bufL[(size_t) writePos] = wl;
        bufR[(size_t) writePos] = wr;
        writePos = (writePos + 1) % size;

        const float wet = infinite ? 0.85f : 0.65f;
        l += dl * wet;
        r += dr * wet;
    }

private:
    double sampleRate = 44100.0;
    std::vector<float> bufL, bufR;
    int size = 1, writePos = 0;
    double delay = 1000.0, targetDelay = 1000.0;
    float feedGain = 0.0f, feedTarget = 0.0f, lpCoef = 0.5f, lpL = 0.0f, lpR = 0.0f;
    bool infinite = false;
    int infFeedLeft = 0;
};

//==============================================================================
// Zeile 5: rhythmisches Gate
class GateRow : public Row
{
public:
    void prepare (double sr) override
    {
        coef = 1.0f - std::exp (-1.0f / (0.0015f * (float) sr));
        g = 1.0f;
    }

    void trigger (int col, bool fromIdle, const Context&) override
    {
        column = col;
        if (fromIdle) g = 1.0f;
    }

    void process (float& l, float& r, bool engaged, const Context& c) override
    {
        if (! engaged)
            return;

        bool on = true;
        const double b = c.beatPos;
        auto frac = [] (double x) { return x - std::floor (x); };

        if (column <= 5)
        {
            static const double S[6] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
            on = frac (b / S[column]) < 0.5;
        }
        else if (column == 6)
        {
            const double ph = frac (b / 0.5);
            on = ph < 0.42 || (ph >= 0.6667 && ph < 0.88);
        }
        else
        {
            const auto step = (int64_t) std::floor (b / 0.25);
            on = (hash32 (step) % 10u) < 6u && frac (b / 0.25) < 0.8;
        }

        g += ((on ? 1.0f : 0.0f) - g) * coef;
        l *= g;
        r *= g;
    }

private:
    int column = 0;
    float coef = 0.1f, g = 1.0f;
};

//==============================================================================
// Zeile 6: Lowpass / Highpass
class FilterRow : public Row
{
public:
    void prepare (double sr) override
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, 1, 2 };
        lp.prepare (spec);
        hp.prepare (spec);
        lp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        hp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        lp.setResonance (1.4f);
        hp.setResonance (1.4f);
        lpCut = 20000.0; hpCut = 20.0;
        lp.setCutoffFrequency ((float) lpCut);
        hp.setCutoffFrequency ((float) hpCut);
    }

    void trigger (int col, bool fromIdle, const Context& c) override
    {
        static const double LP[4] = { 1800.0, 900.0, 450.0, 200.0 };
        static const double HP[4] = { 400.0, 1000.0, 2500.0, 6000.0 };

        isHp = col >= 4;
        if (isHp) hpTarget = HP[col - 4];
        else      lpTarget = LP[col];

        if (fromIdle)
        {
            lp.reset();
            hp.reset();
            lpCut = isHp ? 20000.0 : lpTarget;
            hpCut = isHp ? hpTarget : 20.0;
            hpMix = isHp ? 1.0f : 0.0f;
        }
        smooth = (float) (1.0 - std::exp (-1.0 / std::max (16.0, c.glideSamples)));
    }

    void process (float& l, float& r, bool engaged, const Context&) override
    {
        if (! engaged)
            return;

        const double ny = sampleRate * 0.45;
        lpCut = std::exp (std::log (lpCut) + (std::log (std::min (lpTarget, ny)) - std::log (lpCut)) * smooth);
        hpCut = std::exp (std::log (hpCut) + (std::log (std::min (hpTarget, ny)) - std::log (hpCut)) * smooth);
        hpMix += ((isHp ? 1.0f : 0.0f) - hpMix) * smooth;

        if (++counter >= 16)
        {
            counter = 0;
            lp.setCutoffFrequency ((float) lpCut);
            hp.setCutoffFrequency ((float) hpCut);
        }

        const float ll = lp.processSample (0, l), lr = lp.processSample (1, r);
        const float hl = hp.processSample (0, l), hr = hp.processSample (1, r);
        l = ll + (hl - ll) * hpMix;
        r = lr + (hr - lr) * hpMix;
    }

private:
    juce::dsp::StateVariableTPTFilter<float> lp, hp;
    double sampleRate = 44100.0;
    double lpCut = 20000.0, hpCut = 20.0, lpTarget = 20000.0, hpTarget = 20.0;
    float hpMix = 0.0f, smooth = 0.01f;
    bool isHp = false;
    int counter = 0;
};

//==============================================================================
// Zeile 7: Crush, Downsample, Grain, Freeze, Chaos
class KlangRow : public Row
{
public:
    void prepare (double sr) override
    {
        sampleRate = sr;
        hist.prepare ((int) (sr * 4.0));
        grainLen = 0.08 * sr;
        for (auto& gr : grains) gr = {};
    }

    void trigger (int col, bool, const Context&) override
    {
        if ((col == 5 || col == 6) && col != column)
        {
            for (auto& gr : grains) gr = {};
            spawnCounter = 0.0;
        }
        column = col;
        if (col == 6)
            freezeAnchor = (double) hist.now();
    }

    void process (float& l, float& r, bool engaged, const Context& c) override
    {
        const bool frozen = engaged && column == 6;
        if (! frozen)
            hist.push (l, r);
        if (! engaged)
            return;

        int mode = column;
        if (column == 7)
        {
            static const int chaosModes[5] = { 2, 4, 5, -1, 1 };
            const auto step = (int64_t) std::floor (c.beatPos / 0.25);
            mode = chaosModes[hash32 (step * 7 + 3) % 5u];
        }

        switch (mode)
        {
            case 0: crush (l, r, 6.0f); break;
            case 1: crush (l, r, 4.0f); break;
            case 2: crush (l, r, 3.0f); break;
            case 3: downsample (l, r, 8); break;
            case 4: downsample (l, r, 20); break;
            case 5: granular (l, r, c, false); break;
            case 6: granular (l, r, c, true); break;
            default: break; // trocken (Chaos-Pause)
        }
    }

private:
    struct Grain { bool on = false; double start = 0.0, pos = 0.0; };

    static void crush (float& l, float& r, float bits)
    {
        const float q = std::pow (2.0f, bits - 1.0f);
        l = std::round (l * q) / q;
        r = std::round (r * q) / q;
    }

    void downsample (float& l, float& r, int factor)
    {
        if (++dsCounter >= factor)
        {
            dsCounter = 0;
            holdL = l;
            holdR = r;
        }
        l = holdL;
        r = holdR;
    }

    void granular (float& l, float& r, const Context& c, bool freeze)
    {
        const double rate = c.pitchRatio;

        if ((spawnCounter -= 1.0) <= 0.0)
        {
            spawnCounter = grainLen * 0.5;
            auto& gr = grains[(size_t) nextGrain];
            nextGrain = (nextGrain + 1) % 2;
            gr.on = true;
            gr.pos = 0.0;
            const double need = grainLen * rate + 4.0;
            if (freeze)
            {
                gr.start = freezeAnchor - need - random.nextDouble() * 0.015 * sampleRate;
            }
            else
            {
                const double span = std::min (c.samplesPerBeat, (double) hist.getSize() * 0.5 - need * 2.0);
                gr.start = (double) hist.now() - need - random.nextDouble() * std::max (0.0, span);
            }
        }

        float sl = 0.0f, sr = 0.0f;
        for (auto& gr : grains)
        {
            if (! gr.on) continue;
            const double ph = gr.pos / grainLen;
            if (ph >= 1.0) { gr.on = false; continue; }
            const auto w = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * ph));
            float a, b;
            hist.read (gr.start + gr.pos * rate, a, b);
            sl += a * w;
            sr += b * w;
            gr.pos += 1.0;
        }
        l = sl;
        r = sr;
    }

    double sampleRate = 44100.0;
    History hist;
    int column = 0;
    int dsCounter = 0;
    float holdL = 0.0f, holdR = 0.0f;
    std::array<Grain, 2> grains;
    int nextGrain = 0;
    double grainLen = 3000.0, spawnCounter = 0.0, freezeAnchor = 0.0;
    juce::Random random { 1234 };
};

//==============================================================================
class Engine
{
public:
    struct Globals
    {
        float mix = 1.0f, drive = 0.0f, outGain = 1.0f;
        double attackSamples = 128.0, releaseSamples = 512.0;
    };

    Engine()
    {
        for (int i = 0; i < 4; ++i)
            rows[(size_t) i] = std::make_unique<LoopRow> (i);
        rows[4] = std::make_unique<EchoRow>();
        rows[5] = std::make_unique<GateRow>();
        rows[6] = std::make_unique<FilterRow>();
        rows[7] = std::make_unique<KlangRow>();
    }

    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& r : rows) r->prepare (sr);
        active.fill (false);
        amount.fill (0.0f);
        target.fill (1.0f);
    }

    void trigger (int row, int col, float vel, const Context& c)
    {
        const bool fromIdle = ! active[(size_t) row] && amount[(size_t) row] <= 0.0f;
        active[(size_t) row] = true;
        target[(size_t) row] = juce::jlimit (0.05f, 1.0f, vel);
        rows[(size_t) row]->trigger (col, fromIdle, c);
    }

    void release (int row)
    {
        if (! active[(size_t) row]) return;
        active[(size_t) row] = false;
        rows[(size_t) row]->release();
    }

    void processSample (float& l, float& r, const Context& c, const Globals& g)
    {
        const float dryL = l, dryR = r;
        const auto up = (float) (1.0 / std::max (16.0, g.attackSamples));
        const auto down = (float) (1.0 / std::max (16.0, g.releaseSamples));
        float activity = 0.0f;

        for (size_t i = 0; i < (size_t) kRows; ++i)
        {
            const bool engaged = active[i] || amount[i] > 0.0f;
            float wl = l, wr = r;
            rows[i]->process (wl, wr, engaged, c);
            if (! engaged)
                continue;

            if (active[i])
                amount[i] += juce::jlimit (-down, up, target[i] - amount[i]);
            else
            {
                amount[i] -= down;
                if (amount[i] <= 0.0f)
                {
                    amount[i] = 0.0f;
                    rows[i]->stop();
                }
            }

            l += (wl - l) * amount[i];
            r += (wr - r) * amount[i];
            activity = std::max (activity, amount[i]);
        }

        if (activity > 0.0f && g.drive > 0.001f)
        {
            const float k = 1.0f + g.drive * 7.0f;
            const float comp = 1.0f / std::sqrt (k);
            const float cl = std::tanh (l * k) * comp;
            const float cr = std::tanh (r * k) * comp;
            l += (cl - l) * activity;
            r += (cr - r) * activity;
        }

        l = (dryL + (l - dryL) * g.mix) * g.outGain;
        r = (dryR + (r - dryR) * g.mix) * g.outGain;
    }

private:
    double sampleRate = 44100.0;
    std::array<std::unique_ptr<Row>, kRows> rows;
    std::array<bool, kRows> active {};
    std::array<float, kRows> amount {}, target {};
};

} // namespace earr
