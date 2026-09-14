#pragma once

#include <JuceHeader.h>

//==============================================================================
// A 4th-order Linkwitz-Riley crossover (two cascaded 2nd-order Butterworth
// stages), used to split a mono signal into a low band and a high band at a
// given frequency. Two of these in series give us a clean 3-band split.
//==============================================================================
struct LR4Crossover
{
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        lp1.prepare (spec);
        lp2.prepare (spec);
        hp1.prepare (spec);
        hp2.prepare (spec);
        update (spec.sampleRate, frequency);
    }

    void update (double newSampleRate, float newFrequency)
    {
        sampleRate = newSampleRate;
        frequency = newFrequency;

        auto lpCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, frequency);
        auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, frequency);

        *lp1.coefficients = *lpCoeffs;
        *lp2.coefficients = *lpCoeffs;
        *hp1.coefficients = *hpCoeffs;
        *hp2.coefficients = *hpCoeffs;
    }

    void reset()
    {
        lp1.reset();
        lp2.reset();
        hp1.reset();
        hp2.reset();
    }

    // Returns { low, high }
    std::pair<float, float> processSample (float x) noexcept
    {
        const float low  = lp2.processSample (lp1.processSample (x));
        const float high = hp2.processSample (hp1.processSample (x));
        return { low, high };
    }

    float frequency = 1000.0f;
    double sampleRate = 44100.0;

    juce::dsp::IIR::Filter<float> lp1, lp2, hp1, hp2;
};

//==============================================================================
class ChocolaAudioProcessor : public juce::AudioProcessor
{
public:
    ChocolaAudioProcessor();
    ~ChocolaAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // 0.0 - 1.0, smoothed output level for the UI's reactive dot.
    float getOutputLevel() const noexcept { return outputLevel.load(); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    double currentSampleRate = 44100.0;

    // ---- Stage 1: Haas pan crossover (fixed at 2 kHz / 8 kHz) ----
    LR4Crossover panSplitLow { 2000.0f };
    LR4Crossover panSplitHigh { 8000.0f };

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> midHaasDelay { 4096 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> highHaasDelay { 512 };

    static constexpr float midHaasMs  = 40.0f;
    static constexpr float highHaasMs = 2.0f;

    // ---- Stage 2: Width crossover on the Mid/Side "side" signal (120 Hz / 6 kHz) ----
    LR4Crossover widthSplitLow { 120.0f };
    LR4Crossover widthSplitHigh { 6000.0f };

    // Pushed further than before for a bigger, crazier image. Below 120 Hz is
    // still forced to 0 (mono) so there's no low-end mud or phase cancellation.
    static constexpr float widthMidGain  = 1.55f;
    static constexpr float widthHighGain = 2.1f;
    // Extra makeup width applied after the band shaping, tamed by a soft
    // clipper below so the boosted side signal doesn't turn harsh or clip.
    static constexpr float superWidthBoost = 1.25f;

    // ---- Stage 3: Chorus (Auto-Chroma style modulated doubling) ----
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> chorusDelayL { 4096 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> chorusDelayR { 4096 };
    float chorusPhaseL = 0.0f;
    float chorusPhaseR = 0.37f; // offset so the two voices don't move together

    // ---- Stage 4: extra decorrelation all-pass on the side signal, for even
    // more width without adding any comb/EQ coloration (it's phase-only). ----
    juce::dsp::IIR::Filter<float> sideAllpass1, sideAllpass2;

    std::atomic<float> outputLevel { 0.0f };

    // LED meter envelope follower - same fast-attack/slower-release +
    // tanh gate mechanic as HYPERSCAPE's activity LED, just fed from
    // this plugin's own output signal, so the dot reacts identically.
    float meterEnvelope = 0.0f;
    float meterAttackCoeff = 0.0f;
    float meterReleaseCoeff = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChocolaAudioProcessor)
};
