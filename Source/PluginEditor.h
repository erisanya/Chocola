#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class ChocolaAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit ChocolaAudioProcessorEditor (ChocolaAudioProcessor&);
    ~ChocolaAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ChocolaAudioProcessor& audioProcessor;

    juce::Slider mixKnob;
    juce::Label title;
    juce::Label subtitle;
    juce::Label mixCaption;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

    // Small "alive" LED that pulses with the plugin's own output level -
    // same mechanism/look as HYPER SCAPE and CloudOne, just recoloured.
    juce::Point<float> ledCentre;
    float ledLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChocolaAudioProcessorEditor)
};
