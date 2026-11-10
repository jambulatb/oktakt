// Standalone app: our own window (no "Options" button — audio settings live in the gear menu)

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include "SystemAudio.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "ui/Editor.h"
#include "BinaryData.h"

namespace
{

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (juce::StandalonePluginHolder& holder)
        : DocumentWindow ("OKTAKT", ok::ui::C::bg, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
       #if JUCE_LINUX
        // Windows and macOS take the icon from the executable; on Linux the window carries it
        setIcon (juce::ImageCache::getFromMemory (BinaryData::icon256_png, BinaryData::icon256_pngSize));
       #endif

        auto* editor = holder.processor->createEditorIfNeeded();
        constexpr int w = ok::ui::Editor::kWidth, h = ok::ui::Editor::kHeight;

        // Fit the screen: 75…100 % on first open
        float scale = 1.0f;
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            auto area = display->userArea;
            scale = juce::jlimit (0.75f, 1.0f, juce::jmin ((float) (area.getHeight() - 60) / h, (float) (area.getWidth() - 20) / w));
        }
        editor->setSize ((int) std::lround (w * scale), (int) std::lround (h * scale));
        setContentOwned (editor, true);

        setResizable (true, false);
        setResizeLimits (w * 3 / 4, h * 3 / 4, w * 3 / 2, h * 3 / 2);
        getConstrainer()->setFixedAspectRatio ((double) w / h);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        editor->grabKeyboardFocus();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class OktaktApp : public juce::JUCEApplication
{
public:
    OktaktApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "OKTAKT";
        options.filenameSuffix = ".settings";
       #if JUCE_LINUX
        options.folderName = "~/.config/OKTAKT";
       #else
        options.folderName = "OKTAKT";   // Windows: %APPDATA%\OKTAKT
       #endif
        options.osxLibrarySubFolder = "Application Support";
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override { return "OKTAKT"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        // The output picked in Settings (a desktop sound output), if it is still there
        auto saved = appProperties.getUserSettings()->getValue ("systemOutput");
        if (saved.isNotEmpty())
            for (auto& o : ok::systemaudio::listOutputs())
                if (o.id == saved) { ok::systemaudio::routeTo (saved); break; }

        holder = std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false);
        window = std::make_unique<MainWindow> (*holder);
    }

    void shutdown() override
    {
        if (holder != nullptr) holder->savePluginState();
        window = nullptr;
        holder = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        // Unsaved changes: ask first (Save / Don't save / Cancel)
        auto* editor = window != nullptr ? dynamic_cast<ok::ui::Editor*> (window->getContentComponent()) : nullptr;
        if (editor != nullptr)
        {
            editor->confirmClose ([this] { quitNow(); });
            return;
        }
        quitNow();
    }

    void quitNow()
    {
        if (holder != nullptr) holder->savePluginState();
        appProperties.saveIfNeeded();
        quit();
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandalonePluginHolder> holder;
    std::unique_ptr<MainWindow> window;
};

} // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new OktaktApp(); }

#endif
