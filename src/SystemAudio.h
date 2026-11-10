#pragma once

#include <juce_core/juce_core.h>

// The audio outputs the desktop shows in its sound settings (PipeWire / PulseAudio sinks),
// instead of the long raw ALSA device list. Linux only; elsewhere the list is empty.
namespace ok::systemaudio
{

struct Output
{
    juce::String id;            // sink name, e.g. alsa_output.pci-0000_00_1b.0.analog-stereo
    juce::String description;   // what the system shows, e.g. Built-in Audio Analog Stereo
};

// Asks the sound server; empty when there is none (plain ALSA) or it does not answer quickly
std::vector<Output> listOutputs();

// Routes the streams this app opens through the system "default" device to the given output.
// Empty id: follow the system's own choice. Takes effect when the audio device is reopened.
void routeTo (const juce::String& outputId);
juce::String routedTo();

} // namespace ok::systemaudio
