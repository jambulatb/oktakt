#include "SystemAudio.h"

#if JUCE_LINUX
 #include <cstdlib>
#endif

namespace ok::systemaudio
{

#if JUCE_LINUX
namespace
{
// The few libpulse calls we need, loaded at run time (libpulse.so.0 ships with every desktop
// that runs PipeWire or PulseAudio), so building needs no extra headers.
struct pa_sink_info_head { const char* name; uint32_t index; const char* description; };

using MainloopNew     = void* (*)();
using MainloopFree    = void (*) (void*);
using MainloopGetApi  = void* (*) (void*);
using MainloopIterate = int (*) (void*, int, int*);
using ContextNew      = void* (*) (void*, const char*);
using ContextConnect  = int (*) (void*, const char*, int, const void*);
using ContextGetState = int (*) (void*);
using ContextDisconnect = void (*) (void*);
using ContextUnref    = void (*) (void*);
using SinkInfoCb      = void (*) (void*, const pa_sink_info_head*, int, void*);
using GetSinkInfoList = void* (*) (void*, SinkInfoCb, void*);
using OperationGetState = int (*) (void*);
using OperationUnref  = void (*) (void*);

constexpr int kNoAutospawn = 1;
constexpr int kContextReady = 4, kContextFailed = 5, kContextTerminated = 6;
constexpr int kOperationRunning = 0;
}

std::vector<Output> listOutputs()
{
    std::vector<Output> out;
    juce::DynamicLibrary lib;
    if (! lib.open ("libpulse.so.0")) return out;

    auto mainloopNew     = (MainloopNew)     lib.getFunction ("pa_mainloop_new");
    auto mainloopFree    = (MainloopFree)    lib.getFunction ("pa_mainloop_free");
    auto mainloopGetApi  = (MainloopGetApi)  lib.getFunction ("pa_mainloop_get_api");
    auto mainloopIterate = (MainloopIterate) lib.getFunction ("pa_mainloop_iterate");
    auto contextNew      = (ContextNew)      lib.getFunction ("pa_context_new");
    auto contextConnect  = (ContextConnect)  lib.getFunction ("pa_context_connect");
    auto contextGetState = (ContextGetState) lib.getFunction ("pa_context_get_state");
    auto contextDisconnect = (ContextDisconnect) lib.getFunction ("pa_context_disconnect");
    auto contextUnref    = (ContextUnref)    lib.getFunction ("pa_context_unref");
    auto getSinkInfoList = (GetSinkInfoList) lib.getFunction ("pa_context_get_sink_info_list");
    auto operationGetState = (OperationGetState) lib.getFunction ("pa_operation_get_state");
    auto operationUnref  = (OperationUnref)  lib.getFunction ("pa_operation_unref");
    if (! (mainloopNew && mainloopFree && mainloopGetApi && mainloopIterate && contextNew && contextConnect && contextGetState
           && contextDisconnect && contextUnref && getSinkInfoList && operationGetState && operationUnref))
        return out;

    auto* loop = mainloopNew();
    if (loop == nullptr) return out;
    auto* ctx = contextNew (mainloopGetApi (loop), "OKTAKT");
    if (ctx == nullptr) { mainloopFree (loop); return out; }

    // Never block the UI for long: a silent server gives up after ~1 s
    const auto deadline = juce::Time::getMillisecondCounter() + 1000;
    auto pump = [&] (auto&& done)
    {
        while (! done())
        {
            if (mainloopIterate (loop, 0, nullptr) < 0 || juce::Time::getMillisecondCounter() > deadline) return false;
            if (! done()) juce::Thread::sleep (1);
        }
        return true;
    };

    if (contextConnect (ctx, nullptr, kNoAutospawn, nullptr) >= 0)
    {
        int state = 0;
        const bool ready = pump ([&] { state = contextGetState (ctx); return state == kContextReady || state == kContextFailed || state == kContextTerminated; })
                        && state == kContextReady;
        if (ready)
        {
            SinkInfoCb cb = [] (void*, const pa_sink_info_head* i, int eol, void* ud)
            {
                if (eol != 0 || i == nullptr || i->name == nullptr) return;
                auto& list = *static_cast<std::vector<Output>*> (ud);
                juce::String desc = juce::String::fromUTF8 (i->description != nullptr ? i->description : i->name);
                list.push_back ({ juce::String::fromUTF8 (i->name), desc });
            };
            if (auto* op = getSinkInfoList (ctx, cb, &out))
            {
                if (! pump ([&] { return operationGetState (op) != kOperationRunning; })) out.clear();
                operationUnref (op);
            }
        }
        contextDisconnect (ctx);
    }
    contextUnref (ctx);
    mainloopFree (loop);
    return out;
}

void routeTo (const juce::String& outputId)
{
    // PULSE_SINK for the PulseAudio ALSA plugin, PIPEWIRE_NODE for the PipeWire one:
    // whichever the system "default" device goes through picks the output up when it opens
    for (auto* var : { "PULSE_SINK", "PIPEWIRE_NODE" })
    {
        if (outputId.isEmpty()) ::unsetenv (var);
        else ::setenv (var, outputId.toRawUTF8(), 1);
    }
}

juce::String routedTo()
{
    auto* v = ::getenv ("PULSE_SINK");
    return v != nullptr ? juce::String::fromUTF8 (v) : juce::String();
}

#else
std::vector<Output> listOutputs() { return {}; }
void routeTo (const juce::String&) {}
juce::String routedTo() { return {}; }
#endif

} // namespace ok::systemaudio
