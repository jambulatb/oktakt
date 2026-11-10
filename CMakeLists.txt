cmake_minimum_required(VERSION 3.24)

# macOS: runs on macOS 11 and newer (set before project() so every target picks it up)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS OKTAKT runs on")

project(OKTAKT VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Windows: link the C++ runtime statically, so OKTAKT.exe and the VST3 run without the VC++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# On Windows the plugin folder (C:\Program Files\Common Files\VST3) needs admin rights: copy by hand there
if(WIN32)
    set(OKTAKT_INSTALL_DEFAULT OFF)
else()
    set(OKTAKT_INSTALL_DEFAULT ON)
endif()
option(OKTAKT_INSTALL_PLUGINS "Copy VST3 and CLAP to the user's plugin folders after building" ${OKTAKT_INSTALL_DEFAULT})
option(OKTAKT_CLAP "Build the CLAP plugin" ON)

set(EXT ${CMAKE_CURRENT_SOURCE_DIR}/external)
foreach(dep JUCE signalsmith-linear signalsmith-stretch)
    if(NOT EXISTS ${EXT}/${dep}/CMakeLists.txt)
        message(FATAL_ERROR "Missing external/${dep}. Run ./scripts/fetch-deps.sh first.")
    endif()
endforeach()

add_subdirectory(${EXT}/JUCE)
add_subdirectory(${EXT}/signalsmith-linear EXCLUDE_FROM_ALL)
add_subdirectory(${EXT}/signalsmith-stretch EXCLUDE_FROM_ALL)

# App icon (resources/icon). Windows: our own multi-size OKTAKT.ico goes in through OKTAKT.rc below,
# so JUCE is not asked to generate a cruder one from the PNGs there. macOS: JUCE builds the .icns.
# Linux: the standalone window sets icon-256.png itself.
# Formats: AU (Logic, GarageBand) only exists on macOS
set(OKTAKT_FORMATS Standalone VST3)
if(APPLE)
    list(APPEND OKTAKT_FORMATS AU)
endif()

if(WIN32)
    set(OKTAKT_ICON_ARGS)
else()
    set(OKTAKT_ICON_ARGS ICON_BIG resources/icon/icon-1024.png ICON_SMALL resources/icon/icon-256.png)
endif()

juce_add_binary_data(OktaktFonts
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        resources/fonts/IBMPlexSansCondensed-Regular.ttf
        resources/fonts/IBMPlexSansCondensed-Medium.ttf
        resources/fonts/IBMPlexSansCondensed-SemiBold.ttf
        resources/fonts/IBMPlexSansCondensed-Bold.ttf
        resources/fonts/IBMPlexMono-Regular.ttf
        resources/fonts/IBMPlexMono-SemiBold.ttf
        resources/icon/icon-256.png)

juce_add_plugin(OKTAKT
    COMPANY_NAME "OKTAKT"
    PRODUCT_NAME "OKTAKT"
    BUNDLE_ID "com.oktakt.sampler"
    PLUGIN_MANUFACTURER_CODE Oktk
    PLUGIN_CODE Okt1
    FORMATS ${OKTAKT_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Sampler Drum
    ${OKTAKT_ICON_ARGS}
    COPY_PLUGIN_AFTER_BUILD ${OKTAKT_INSTALL_PLUGINS})

# Windows: OKTAKT.exe's first icon resource is what Explorer, the title bar and the taskbar show
if(WIN32)
    target_sources(OKTAKT_Standalone PRIVATE resources/icon/OKTAKT.rc)
endif()

set(OKTAKT_SOURCES
    src/Model.cpp
    src/SampleStore.cpp
    src/Engine.cpp
    src/Session.cpp
    src/PluginProcessor.cpp
    src/SystemAudio.cpp
    src/ui/Theme.cpp
    src/ui/Widgets.cpp
    src/ui/Header.cpp
    src/ui/Browser.cpp
    src/ui/GridView.cpp
    src/ui/SidePanel.cpp
    src/ui/SoundPanel.cpp
    src/ui/Settings.cpp
    src/ui/Editor.cpp)

target_sources(OKTAKT PRIVATE ${OKTAKT_SOURCES} src/StandaloneApp.cpp)

target_include_directories(OKTAKT PRIVATE src)
target_compile_definitions(OKTAKT PRIVATE OKTAKT_DEMO_KIT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/demo-kit")

target_compile_definitions(OKTAKT PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    $<$<PLATFORM_ID:Linux>:JUCE_ALSA=1>
    $<$<PLATFORM_ID:Linux>:JUCE_JACK=1>
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    JUCE_MODAL_LOOPS_PERMITTED=0)

# MSVC: sources are UTF-8; big translation units
if(MSVC)
    target_compile_options(OKTAKT PRIVATE /utf-8 /bigobj /MP)
endif()

target_link_libraries(OKTAKT
    PRIVATE
        OktaktFonts
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_dsp
        signalsmith-stretch
    PUBLIC
        juce::juce_recommended_config_flags)

if(OKTAKT_CLAP)
    if(NOT EXISTS ${EXT}/clap-juce-extensions/CMakeLists.txt)
        message(FATAL_ERROR "Missing external/clap-juce-extensions. Run ./scripts/fetch-deps.sh first.")
    endif()
    add_subdirectory(${EXT}/clap-juce-extensions EXCLUDE_FROM_ALL)
    clap_juce_extensions_plugin(TARGET OKTAKT
        CLAP_ID "com.oktakt.sampler"
        CLAP_FEATURES instrument sampler drum-machine stereo)
endif()

# Offline self-check: renders the engine and saves UI snapshots (no audio device, no windows)
option(OKTAKT_TESTS "Build the OKTAKT_Check self-test tool" OFF)
if(OKTAKT_TESTS)
    juce_add_console_app(OKTAKT_Check PRODUCT_NAME "OKTAKT_Check")
    target_sources(OKTAKT_Check PRIVATE tests/Check.cpp ${OKTAKT_SOURCES})
    target_include_directories(OKTAKT_Check PRIVATE src)
    if(MSVC)
        target_compile_options(OKTAKT_Check PRIVATE /utf-8 /bigobj /MP)
    endif()
    target_compile_definitions(OKTAKT_Check PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1
        JUCE_PLUGINHOST_VST3=1
        JucePlugin_Build_Standalone=0)
    target_link_libraries(OKTAKT_Check PRIVATE
        OktaktFonts
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        signalsmith-stretch
        juce::juce_recommended_config_flags)
endif()
