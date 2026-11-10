#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "Widgets.h"

namespace ok::ui
{

class Settings : public juce::Component, private juce::ChangeListener
{
public:
    Settings (Session&, std::function<void()> onClose);
    ~Settings() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void fill();
    void applySetup (std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)> change);

    Session& session;
    std::function<void()> onClose;
    juce::AudioDeviceManager* dm = nullptr;
    juce::PropertySet* store = nullptr;
    juce::StringArray outputIds;   // per item of `device` in system mode: "" = system default
    bool systemMode = false;       // `device` lists the desktop's outputs, not raw ALSA devices
    juce::String alsaDefaultName() const;
    void chooseSystemOutput (const juce::String& id);

    Btn closeBtn, accent;
    juce::ComboBox driver, device, rate, buffer, meter;
    juce::Slider metroVol;
    juce::String latencyText;
    juce::Rectangle<int> audioTitle, metroTitle, textArea, labels[7], latencyArea, volText;
    bool filling = false;
};

} // namespace ok::ui
