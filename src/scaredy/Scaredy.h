#pragma once
#include "common/FxBase.h"
#include "core/ScaredyCore.h"

// Scaredy Cat: a 3-band volume shaper (sidechain-style pump without needing a sidechain). Original design and code.
// Drive > 3-band split > per-band envelopes > limiter > hidden soft clipper. AMOUNT 0 is an exact bypass.
class Scaredy : public catfx::FxBase, private juce::AsyncUpdater, private juce::ValueTree::Listener
{
public:
    Scaredy();
    ~Scaredy() override;
    void prepare(double sr, int maxBlock) override;
    void process(juce::AudioBuffer<float>&, const juce::AudioBuffer<float>*) override;
    juce::AudioProcessorEditor* createEditor() override;
    std::vector<Preset> presets() const override { return {}; }   // the editor has its own preset list (they carry the three envelopes too)
    juce::StringArray randomExclude() const override { return { "amount", "power" }; }

    // --- envelopes (message thread) ---
    const scaredy::Curve& curve(int lane) const { return curves[(size_t) lane]; }
    void setCurve(int lane, scaredy::Curve c, bool alsoOthers = false);
    std::function<void()> onCurvesChanged;                  // set by the editor
    // --- presets ---
    static int numFactoryPresets();
    static juce::String factoryPresetName(int i);
    void applyFactoryPreset(int i);
    juce::File presetFolder() const;
    juce::StringArray userPresetNames() const;
    bool saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::String& name);
    // --- state kept next to the parameters ---
    juce::var getProp(const juce::String& k, const juce::var& def) const { return apvts.state.getProperty(k, def); }
    void setProp(const juce::String& k, const juce::var& v) { apvts.state.setProperty(k, v, nullptr); }

    // --- meters and displays (read by the editor on the message thread) ---
    scaredy::Engine engine;
    std::atomic<float> peakL { 0.f }, peakR { 0.f };
    std::atomic<bool> viewActive { false };
    static constexpr int kRing = 8192;
    std::atomic<float> ring[3][kRing];                       // mono input, sidechain, output (newest at wpos - 1)
    std::atomic<unsigned> wpos { 0 };
    double sampleRate() const { return fs; }
    static const std::vector<juce::String>& speedNames();
    static double speedBeats(int idx);

private:
    void handleAsyncUpdate() override;
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected(juce::ValueTree&) override { loadCurvesFromState(); }
    void loadCurvesFromState();
    void publish();
    scaredy::Params readParams() const;

    std::array<scaredy::Curve, 3> curves;
    // triple buffer of baked tables: the message thread writes, the audio thread reads, nothing blocks
    static constexpr int kTab = 3 * (scaredy::kTable + 1);
    float master[kTab];
    float bufs[3][kTab];
    std::atomic<int> mid { 2 };
    int wIdx = 0, rIdx = 1;
    std::atomic<int> reportedLatency { -1 };
    bool ignoreState = false;
    // cached parameter pointers
    std::atomic<float> *pAmount, *pLo, *pMid, *pHi, *pXlo, *pXhi, *pHz, *pSwing, *pSmooth, *pDrive, *pCeil, *pOffset, *pThresh,
        *pSpeed, *pSync, *pTrig, *pSlope, *pMulti, *pLinear, *pDtype, *pLimon, *pOffunit, *pLinked, *pSolo, *pSctrig, *pPower;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Scaredy)
};
