#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Session.h"

class OktaktProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    OktaktProcessor();
    ~OktaktProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OKTAKT"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool isPluginBuild() const { return pluginMode; }

    ok::Engine engine;
    std::unique_ptr<ok::Session> session;

private:
    static BusesProperties makeBuses();
    void timerCallback() override;
    void refreshStateCache();

    // Hosts may ask for the state from any thread: hand them a copy made on the message thread
    juce::CriticalSection stateLock;
    juce::String stateCache;
    juce::uint32 cachedRevision = 0;
    const bool pluginMode;
    std::vector<float*> outPtrs, chunk;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OktaktProcessor)
};
