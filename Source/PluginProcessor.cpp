#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ChocolaAudioProcessor::ChocolaAudioProcessor()
     : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

ChocolaAudioProcessor::~ChocolaAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout ChocolaAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 },
        "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value)); })
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String ChocolaAudioProcessor::getName() const { return JucePlugin_Name; }
bool ChocolaAudioProcessor::acceptsMidi() const { return false; }
bool ChocolaAudioProcessor::producesMidi() const { return false; }
bool ChocolaAudioProcessor::isMidiEffect() const { return false; }
double ChocolaAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ChocolaAudioProcessor::getNumPrograms() { return 1; }
int ChocolaAudioProcessor::getCurrentProgram() { return 0; }
void ChocolaAudioProcessor::setCurrentProgram (int) {}
const juce::String ChocolaAudioProcessor::getProgramName (int) { return {}; }
void ChocolaAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void ChocolaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = 1; // everything here runs as mono per-channel processing

    panSplitLow.prepare (spec);
    panSplitHigh.prepare (spec);
    widthSplitLow.prepare (spec);
    widthSplitHigh.prepare (spec);

    midHaasDelay.prepare (spec);
    highHaasDelay.prepare (spec);
    chorusDelayL.prepare (spec);
    chorusDelayR.prepare (spec);

    sideAllpass1.prepare (spec);
    sideAllpass2.prepare (spec);
    // Two cascaded all-pass stages purely to smear the phase of the side
    // signal a little — no tonal change, no mono-compatibility cost, just
    // extra decorrelation on top of the Haas/M-S widening for a bigger image.
    *sideAllpass1.coefficients = *juce::dsp::IIR::Coefficients<float>::makeAllPass (sampleRate, 700.0f, 0.7f);
    *sideAllpass2.coefficients = *juce::dsp::IIR::Coefficients<float>::makeAllPass (sampleRate, 3000.0f, 0.7f);
    sideAllpass1.reset();
    sideAllpass2.reset();

    midHaasDelay.setMaximumDelayInSamples ((int) (0.06 * sampleRate));
    highHaasDelay.setMaximumDelayInSamples ((int) (0.02 * sampleRate));
    chorusDelayL.setMaximumDelayInSamples ((int) (0.05 * sampleRate));
    chorusDelayR.setMaximumDelayInSamples ((int) (0.05 * sampleRate));

    midHaasDelay.setDelay ((float) (midHaasMs * 0.001 * sampleRate));
    highHaasDelay.setDelay ((float) (highHaasMs * 0.001 * sampleRate));

    midHaasDelay.reset();
    highHaasDelay.reset();
    chorusDelayL.reset();
    chorusDelayR.reset();

    panSplitLow.reset();
    panSplitHigh.reset();
    widthSplitLow.reset();
    widthSplitHigh.reset();

    // Same envelope timing as HYPERSCAPE's activity LED: fast attack
    // (~1ms), slower release (~20ms).
    meterAttackCoeff  = std::exp (-1.0f / (static_cast<float> (sampleRate) * 0.001f));
    meterReleaseCoeff = std::exp (-1.0f / (static_cast<float> (sampleRate) * 0.02f));
    meterEnvelope = 0.0f;
}

void ChocolaAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ChocolaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}
#endif

void ChocolaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numInputChannels = getTotalNumInputChannels();

    const float mixParam = apvts.getRawParameterValue ("mix")->load();
    const float mix = juce::jlimit (0.0f, 1.0f, mixParam / 100.0f);

    auto* left  = buffer.getWritePointer (0);
    auto* right = numInputChannels > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0);

    // Chorus LFO settings: slow, subtle modulation depth on top of a base delay.
    const float chorusBaseMs  = 18.0f;
    const float chorusDepthMs = 6.0f;
    const float chorusRateHz  = 0.25f;
    const float phaseInc = (float) (juce::MathConstants<float>::twoPi * chorusRateHz / currentSampleRate);

    float lastGate = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const float dryL = left[n];
        const float dryR = right[n];

        // ---- Build the mono source that feeds the "super wide" algorithm ----
        const float monoIn = 0.5f * (dryL + dryR);

        // ---- Stage 1: split into low / mid / high, hard-Haas the mid & high bands ----
        auto [low, aboveLow]   = panSplitLow.processSample (monoIn);
        auto [mid, high]       = panSplitHigh.processSample (aboveLow);

        midHaasDelay.pushSample (0, mid);
        highHaasDelay.pushSample (0, high);
        const float midDelayed  = midHaasDelay.popSample (0);
        const float highDelayed = highHaasDelay.popSample (0);

        // Right channel gets the bands direct; left channel gets them delayed.
        // Low band stays centered. This inter-channel micro-delay is what
        // creates the Haas-style "super wide" pan without losing mono energy.
        float wetL = low + midDelayed + highDelayed;
        float wetR = low + mid + high;

        // ---- Stage 2: Mid/Side width shaping ----
        float wMid  = 0.5f * (wetL + wetR);
        float wSide = 0.5f * (wetL - wetR);

        auto [sideLow, sideAboveLow] = widthSplitLow.processSample (wSide);
        auto [sideMid, sideHigh]     = widthSplitHigh.processSample (sideAboveLow);

        float shapedSide = (sideLow * 0.0f) + (sideMid * widthMidGain) + (sideHigh * widthHighGain);

        // Extra decorrelation + makeup boost for an even bigger image, soft-clipped
        // so the wider/louder side signal stays smooth instead of turning harsh.
        shapedSide = sideAllpass2.processSample (sideAllpass1.processSample (shapedSide));
        shapedSide = std::tanh (shapedSide * superWidthBoost);

        wetL = wMid + shapedSide;
        wetR = wMid - shapedSide;

        // ---- Stage 3: chorus doubling (Auto-Chroma style) ----
        chorusPhaseL += phaseInc;
        chorusPhaseR += phaseInc;
        if (chorusPhaseL > juce::MathConstants<float>::twoPi) chorusPhaseL -= juce::MathConstants<float>::twoPi;
        if (chorusPhaseR > juce::MathConstants<float>::twoPi) chorusPhaseR -= juce::MathConstants<float>::twoPi;

        const float delayLms = chorusBaseMs + chorusDepthMs * std::sin (chorusPhaseL);
        const float delayRms = chorusBaseMs + chorusDepthMs * std::sin (chorusPhaseR);

        chorusDelayL.pushSample (0, monoIn);
        chorusDelayR.pushSample (0, monoIn);
        const float chorusL = chorusDelayL.popSample (0, delayLms * 0.001f * (float) currentSampleRate);
        const float chorusR = chorusDelayR.popSample (0, delayRms * 0.001f * (float) currentSampleRate);

        wetL += chorusL * 0.3f;
        wetR -= chorusR * 0.3f; // subtract on R (not add) so the chorus itself adds width instead of just thickness

        // ---- Dry / wet blend driven by the single MIX knob ----
        const float outL = dryL + mix * (wetL - dryL);
        const float outR = dryR + mix * (wetR - dryR);

        left[n]  = outL;
        right[n] = outR;

        const float rectified = std::abs (0.5f * (outL + outR));
        if (rectified > meterEnvelope)
            meterEnvelope = meterAttackCoeff * meterEnvelope + (1.0f - meterAttackCoeff) * rectified;
        else
            meterEnvelope = meterReleaseCoeff * meterEnvelope + (1.0f - meterReleaseCoeff) * rectified;
        lastGate = std::tanh (meterEnvelope * 14.0f);
    }

    outputLevel.store (lastGate, std::memory_order_relaxed);
}

//==============================================================================
bool ChocolaAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* ChocolaAudioProcessor::createEditor()
{
    return new ChocolaAudioProcessorEditor (*this);
}

//==============================================================================
void ChocolaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        std::unique_ptr<juce::XmlElement> xml (state.createXml());
        copyXmlToBinary (*xml, destData);
    }
}

void ChocolaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChocolaAudioProcessor();
}
