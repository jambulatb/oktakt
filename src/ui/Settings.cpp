#include "Settings.h"
#include "../SystemAudio.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace ok::ui
{

Settings::Settings (Session& s, std::function<void()> close) : session (s), onClose (std::move (close))
{
   #if JucePlugin_Build_Standalone
    if (! session.isPlugin)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            dm = &holder->deviceManager;
            store = holder->settings.get();
        }
   #endif

    closeBtn.bg = juce::Colours::transparentBlack;
    closeBtn.radius = 5.0f;
    closeBtn.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour) { drawCross (g, r.withSizeKeepingCentre (9, 9), C::text2, 1.6f); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    closeBtn.setTooltip ("Close settings");
    addAndMakeVisible (closeBtn);

    for (auto* c : { &driver, &device, &rate, &buffer })
    {
        c->setVisible (dm != nullptr);
        addChildComponent (*c);
    }
    driver.onChange = [this]
    {
        if (filling || dm == nullptr) return;
        dm->setCurrentAudioDeviceType (driver.getText(), true);
        fill();
    };
    device.onChange = [this]
    {
        if (filling) return;
        if (systemMode)
        {
            int i = device.getSelectedId() - 1;
            if (juce::isPositiveAndBelow (i, outputIds.size())) chooseSystemOutput (outputIds[i]);
            return;
        }
        auto name = device.getText();
        applySetup ([name] (auto& st) { st.outputDeviceName = name; st.inputDeviceName = {}; st.useDefaultInputChannels = false; st.inputChannels.clear(); });
    };
    rate.onChange = [this]
    {
        if (filling) return;
        double sr = rate.getSelectedId();
        applySetup ([sr] (auto& st) { st.sampleRate = sr; });
    };
    buffer.onChange = [this]
    {
        if (filling) return;
        int b = buffer.getSelectedId();
        applySetup ([b] (auto& st) { st.bufferSize = b; });
    };

    metroVol.setSliderStyle (juce::Slider::LinearHorizontal);
    metroVol.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    metroVol.setRange (0, 100, 1);
    metroVol.setValue (session.metronomeVolume, juce::dontSendNotification);
    metroVol.onValueChange = [this]
    {
        session.metronomeVolume = (int) metroVol.getValue();
        session.engine.metronomeVolume = session.metronomeVolume / 100.0f;
        repaint();
    };
    accent.text = "First beat louder";
    accent.font = sans (12.0f, 600);
    accent.radius = 6.0f;
    accent.isOn = [this] { return session.metronomeAccent; };
    accent.onClick = [this]
    {
        session.metronomeAccent = ! session.metronomeAccent;
        session.engine.metronomeAccent = session.metronomeAccent;
        accent.repaint();
    };
    for (int n = 2; n <= 11; ++n) meter.addItem (juce::String (n) + "/4", n);
    meter.setSelectedId (session.metronomeBeats, juce::dontSendNotification);
    meter.setTooltip ("Beats in a metronome bar; with Accent on, the first one is louder");
    meter.onChange = [this]
    {
        session.metronomeBeats = juce::jlimit (2, 11, meter.getSelectedId());
        session.engine.metronomeBeats = session.metronomeBeats;
    };
    if (dm != nullptr)
    {
        addAndMakeVisible (metroVol);
        addAndMakeVisible (meter);
        addAndMakeVisible (accent);
        dm->addChangeListener (this);
        fill();
    }

    setSize (340, dm != nullptr ? 456 : 150);
}

Settings::~Settings()
{
    if (dm != nullptr) dm->removeChangeListener (this);
}

void Settings::changeListenerCallback (juce::ChangeBroadcaster*) { fill(); }

void Settings::applySetup (std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)> change)
{
    if (dm == nullptr) return;
    auto setup = dm->getAudioDeviceSetup();
    change (setup);
    auto err = dm->setAudioDeviceSetup (setup, true);
    if (err.isNotEmpty()) session.toast (err);
    fill();
}

juce::String Settings::alsaDefaultName() const
{
    auto* type = dm != nullptr ? dm->getCurrentDeviceTypeObject() : nullptr;
    if (type == nullptr || type->getTypeName() != "ALSA") return {};
    auto names = type->getDeviceNames (false);
    int i = type->getDefaultDeviceIndex (false);
    return juce::isPositiveAndBelow (i, names.size()) ? names[i] : juce::String();
}

void Settings::chooseSystemOutput (const juce::String& id)
{
    systemaudio::routeTo (id);
    if (store != nullptr) store->setValue ("systemOutput", id);
    // The route is read when the device opens, so reopen it even if it already was "default"
    dm->closeAudioDevice();
    auto name = alsaDefaultName();
    applySetup ([name] (auto& st) { st.outputDeviceName = name; st.inputDeviceName = {}; st.useDefaultInputChannels = false; st.inputChannels.clear(); });
}

void Settings::fill()
{
    if (dm == nullptr) return;
    filling = true;

    driver.clear (juce::dontSendNotification);
    int id = 1;
    for (auto* t : dm->getAvailableDeviceTypes())
    {
        driver.addItem (t->getTypeName(), id);
        if (t->getTypeName() == dm->getCurrentAudioDeviceType()) driver.setSelectedId (id, juce::dontSendNotification);
        ++id;
    }

    device.clear (juce::dontSendNotification);
    outputIds.clear();
    auto setup = dm->getAudioDeviceSetup();
    auto* type = dm->getCurrentDeviceTypeObject();
    if (type != nullptr) type->scanForDevices();

    // ALSA lists every raw PCM (hw, plughw, surround51, dmix…). When a sound server runs,
    // offer the outputs the desktop's sound settings show instead, played through "default".
    auto outputs = alsaDefaultName().isNotEmpty() ? systemaudio::listOutputs() : std::vector<systemaudio::Output>();
    systemMode = ! outputs.empty();
    if (systemMode)
    {
        // "default" and the "pulse" / "pipewire" PCMs all go through the sound server and follow the route
        const auto& cur = setup.outputDeviceName;
        const bool onDefault = cur == alsaDefaultName() || cur.containsIgnoreCase ("PulseAudio") || cur.containsIgnoreCase ("PipeWire");
        const auto routed = systemaudio::routedTo();
        device.addItem ("System default", 1);
        outputIds.add ({});
        for (auto& o : outputs)
        {
            device.addItem (o.description, outputIds.size() + 1);
            outputIds.add (o.id);
        }
        int sel = onDefault ? outputIds.indexOf (routed) : -1;
        if (sel >= 0) device.setSelectedId (sel + 1, juce::dontSendNotification);
        else if (setup.outputDeviceName.isNotEmpty()) device.setText (setup.outputDeviceName, juce::dontSendNotification);
    }
    else if (type != nullptr)
    {
        auto names = type->getDeviceNames (false);
        for (int i = 0; i < names.size(); ++i)
        {
            device.addItem (names[i], i + 1);
            if (names[i] == setup.outputDeviceName) device.setSelectedId (i + 1, juce::dontSendNotification);
        }
    }

    rate.clear (juce::dontSendNotification);
    buffer.clear (juce::dontSendNotification);
    auto* dev = dm->getCurrentAudioDevice();
    juce::Array<double> rates { 44100.0, 48000.0, 96000.0 };
    juce::Array<int> sizes { 64, 128, 256, 512, 1024 };
    for (auto r : rates)
    {
        bool ok = dev == nullptr || dev->getAvailableSampleRates().isEmpty() || dev->getAvailableSampleRates().contains (r);
        if (ok) rate.addItem (juce::String (r / 1000.0, r == 44100.0 ? 1 : 0) + " kHz", (int) r);
    }
    for (auto b : sizes)
    {
        bool ok = dev == nullptr || dev->getAvailableBufferSizes().isEmpty() || dev->getAvailableBufferSizes().contains (b);
        if (ok) buffer.addItem (juce::String (b) + " samples", b);
    }
    if (dev != nullptr)
    {
        rate.setSelectedId ((int) dev->getCurrentSampleRate(), juce::dontSendNotification);
        buffer.setSelectedId (dev->getCurrentBufferSizeSamples(), juce::dontSendNotification);
        if (buffer.getSelectedId() == 0)
            buffer.setText (juce::String (dev->getCurrentBufferSizeSamples()) + " samples", juce::dontSendNotification);
        double ms = (dev->getOutputLatencyInSamples() + dev->getCurrentBufferSizeSamples()) * 1000.0 / juce::jmax (1.0, dev->getCurrentSampleRate());
        latencyText = juce::String::fromUTF8 ("Output latency \xe2\x89\x88 ") + juce::String (ms, 1) + " ms";
    }
    else latencyText = "No audio device is open";

    filling = false;
    repaint();
}

void Settings::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto top = r.removeFromTop (24);
    closeBtn.setBounds (top.removeFromRight (24));
    r.removeFromTop (12);
    audioTitle = r.removeFromTop (14);
    r.removeFromTop (12);
    if (dm == nullptr)
    {
        textArea = r.removeFromTop (60);
        return;
    }
    juce::ComboBox* boxes[] = { &driver, &device, &rate, &buffer };
    for (int i = 0; i < 4; ++i)
    {
        auto row = r.removeFromTop (28);
        labels[i] = row.removeFromLeft (96);
        row.removeFromLeft (10);
        boxes[i]->setBounds (row);
        r.removeFromTop (8);
    }
    latencyArea = r.removeFromTop (14);
    r.removeFromTop (10);
    textArea = r.removeFromTop (64);
    r.removeFromTop (12);
    metroTitle = r.removeFromTop (14);
    r.removeFromTop (12);
    auto row = r.removeFromTop (28);
    labels[4] = row.removeFromLeft (96);
    row.removeFromLeft (10);
    volText = row.removeFromRight (36);
    row.removeFromRight (8);
    metroVol.setBounds (row);
    r.removeFromTop (8);
    row = r.removeFromTop (28);
    labels[5] = row.removeFromLeft (96);
    row.removeFromLeft (10);
    meter.setBounds (row.withWidth (90));
    r.removeFromTop (8);
    row = r.removeFromTop (28);
    labels[6] = row.removeFromLeft (96);
    row.removeFromLeft (10);
    accent.setBounds (row.withWidth (accent.idealWidth (12)));
}

void Settings::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (C::menu);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (C::menuBorder);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (C::text);
    g.setFont (sans (13.0f, 600));
    g.drawText ("Settings", getLocalBounds().reduced (14).removeFromTop (24), juce::Justification::centredLeft, false);

    g.setColour (C::muted);
    g.setFont (caps (11.0f));
    g.drawText ("AUDIO", audioTitle, juce::Justification::centredLeft, false);

    if (dm == nullptr)
    {
        g.setFont (sans (12.0f));
        g.drawFittedText ("In the plugin the output device, sample rate and buffer are set in your DAW.", textArea, juce::Justification::topLeft, 3);
        return;
    }

    const char* names[] = { "Driver", "Output device", "Sample rate", "Buffer size", "Volume", "Time signature", "Accent" };
    g.setFont (sans (12.0f));
    g.setColour (C::text2);
    for (int i = 0; i < 7; ++i) g.drawText (names[i], labels[i], juce::Justification::centredLeft, false);

    g.setColour (C::dim);
    g.setFont (sans (11.0f));
    g.drawText (latencyText, latencyArea, juce::Justification::centredLeft, false);
    g.setColour (C::muted);
    g.drawFittedText (systemMode
                        ? juce::String::fromUTF8 ("Outputs are the ones your system sound settings show. System default follows the choice made there. The main mix plays on channels 1\xe2\x80\x93" "2. To send OKTAKT into another app, pick a virtual output or the JACK driver.")
                       #if JUCE_WINDOWS
                        : juce::String::fromUTF8 ("Drivers and devices are the ones found on this computer (Windows). The main mix plays on channels 1\xe2\x80\x93" "2. To send OKTAKT into another app, pick a virtual cable as the device, for example VB-Cable."),
                       #elif JUCE_MAC
                        : juce::String::fromUTF8 ("Devices are the ones found on this Mac. The main mix plays on channels 1\xe2\x80\x93" "2. To send OKTAKT into another app, pick a virtual device as the output, for example BlackHole."),
                       #else
                        : juce::String::fromUTF8 ("Drivers and devices are the ones found on this computer (Linux). The main mix plays on channels 1\xe2\x80\x93" "2. To send OKTAKT into another app, pick a virtual cable as the device, for example JACK."),
                       #endif
                      textArea, juce::Justification::topLeft, 5);

    g.setColour (C::muted);
    g.setFont (caps (11.0f));
    g.drawText ("METRONOME", metroTitle, juce::Justification::centredLeft, false);
    g.setColour (C::text);
    g.setFont (mono (12.0f));
    g.drawText (juce::String (session.metronomeVolume), volText, juce::Justification::centredRight, false);
}

} // namespace ok::ui
