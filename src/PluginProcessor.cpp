#include "PluginProcessor.h"
#include "ui/Editor.h"

static bool runningStandalone()
{
    return juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone;
}

juce::AudioProcessor::BusesProperties OktaktProcessor::makeBuses()
{
    BusesProperties props;
    props = props.withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    // Separate track outputs only exist in the plugin; standalone plays the main mix on channels 1–2
    if (! runningStandalone())
        for (int i = 2; i <= ok::kNumOutputs; ++i)
            props = props.withOutput ("Out " + juce::String (i), juce::AudioChannelSet::stereo(), false);
    return props;
}

OktaktProcessor::OktaktProcessor()
    : AudioProcessor (makeBuses()), pluginMode (! runningStandalone())
{
    session = std::make_unique<ok::Session> (engine, pluginMode);
    refreshStateCache();
    startTimer (500);
}

void OktaktProcessor::refreshStateCache()
{
    auto json = juce::JSON::toString (session->stateToVar(), true);
    const juce::ScopedLock sl (stateLock);
    stateCache = json;
    cachedRevision = session->revision;
}

void OktaktProcessor::timerCallback()
{
    if (session->revision != cachedRevision) refreshStateCache();
}

OktaktProcessor::~OktaktProcessor()
{
    stopTimer();
    session = nullptr;
}

bool OktaktProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        auto set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo()) return false;
    }
    return layouts.inputBuses.isEmpty() || layouts.getMainInputChannelSet().isDisabled();
}

void OktaktProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, juce::jmax (samplesPerBlock, 2048));
    outPtrs.assign (ok::kNumOutputs * 2, nullptr);
    chunk.assign (ok::kNumOutputs * 2, nullptr);
}

void OktaktProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    buffer.clear();

    const int n = buffer.getNumSamples();
    if (n == 0) return;
    if ((int) outPtrs.size() < ok::kNumOutputs * 2) return;

    std::fill (outPtrs.begin(), outPtrs.end(), nullptr);
    const int numBuses = juce::jmin (getBusCount (false), ok::kNumOutputs);
    for (int b = 0; b < numBuses; ++b)
    {
        auto* bus = getBus (false, b);
        if (bus == nullptr || ! bus->isEnabled() || bus->getNumberOfChannels() < 2) continue;
        int ch = getChannelIndexInProcessBlockBuffer (false, b, 0);
        if (ch + 1 >= buffer.getNumChannels()) continue;
        outPtrs[(size_t) b * 2] = buffer.getWritePointer (ch);
        outPtrs[(size_t) b * 2 + 1] = buffer.getWritePointer (ch + 1);
    }

    ok::Engine::HostInfo host;
    host.isPlugin = pluginMode;
    if (pluginMode)
    {
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                host.playing = pos->getIsPlaying();
                host.bpm = pos->getBpm().orFallback (120.0);
                host.ppq = pos->getPpqPosition().orFallback (0.0);
                if (auto loop = pos->getLoopPoints(); loop && pos->getIsLooping())
                {
                    host.looping = true;
                    host.loopStart = loop->ppqStart;
                    host.loopEnd = loop->ppqEnd;
                }
            }
        }
    }

    // Process in chunks no larger than the prepared block size
    int done = 0;
    while (done < n)
    {
        const int len = juce::jmin (n - done, 2048);
        for (size_t i = 0; i < outPtrs.size(); ++i)
            chunk[i] = outPtrs[i] != nullptr ? outPtrs[i] + done : nullptr;
        if (len > 0) engine.process (chunk.data(), numBuses, len, host);
        host.ppq += len * host.bpm / 60.0 / getSampleRate();
        if (host.looping && host.loopEnd > host.loopStart && host.ppq >= host.loopEnd)
            host.ppq -= host.loopEnd - host.loopStart;   // the engine wrapped there too
        done += len;
    }
}

juce::AudioProcessorEditor* OktaktProcessor::createEditor()
{
    return new ok::ui::Editor (*this);
}

void OktaktProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (juce::MessageManager::existsAndIsCurrentThread()) refreshStateCache();
    const juce::ScopedLock sl (stateLock);
    destData.replaceAll (stateCache.toRawUTF8(), stateCache.getNumBytesAsUTF8());
}

void OktaktProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto text = juce::String::fromUTF8 (static_cast<const char*> (data), sizeInBytes);
    auto v = juce::JSON::parse (text);
    if (! v.isObject()) return;
    {
        const juce::ScopedLock sl (stateLock);
        stateCache = text;
    }

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        session->stateFromVar (v);
    else
        juce::MessageManager::callAsync ([this, v] { session->stateFromVar (v); });
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OktaktProcessor();
}
