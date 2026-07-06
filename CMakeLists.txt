cmake_minimum_required(VERSION 3.22)

project(MasterDesk VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Link the MSVC runtime statically: end-user machines frequently lack the
# VC++ redistributable, and a plugin must not depend on it. Must be set
# before any targets (JUCE, clap-juce-extensions, plugin) are created;
# ignored by non-MSVC toolchains. CMP0091 is NEW with CMake >= 3.22.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MASTERDESK_BUILD_CLAP    "Also build the CLAP format (fetches clap-juce-extensions)" OFF)
option(MASTERDESK_BUILD_TESTS   "Build the offline DSP verification tests"                  ON)
option(MASTERDESK_ENABLE_OPENGL "GPU-accelerated UI rendering where available"              ON)

# ------------------------------------------------------------------ JUCE ----
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.14
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ------------------------------------------------------------- AAX (opt) ----
# Point MASTERDESK_AAX_SDK_PATH (or the AAX_SDK_PATH env var) at the Avid AAX
# SDK to enable the AAX target. AAX also requires PACE signing to load in
# release Pro Tools, which has to happen outside this build.
set(MASTERDESK_AAX_SDK_PATH "" CACHE PATH "Path to the Avid AAX SDK (optional)")
if(MASTERDESK_AAX_SDK_PATH STREQUAL "" AND DEFINED ENV{AAX_SDK_PATH})
    set(MASTERDESK_AAX_SDK_PATH $ENV{AAX_SDK_PATH})
endif()

set(MD_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MD_FORMATS AU)
endif()
if(NOT MASTERDESK_AAX_SDK_PATH STREQUAL "")
    juce_set_aax_sdk_path(${MASTERDESK_AAX_SDK_PATH})
    list(APPEND MD_FORMATS AAX)
endif()

# ---------------------------------------------------------------- plugin ----
juce_add_plugin(MasterDesk
    PRODUCT_NAME               "MasterDesk"
    COMPANY_NAME               "MasterDesk Audio"
    BUNDLE_ID                  "com.masterdeskaudio.masterdesk"
    PLUGIN_MANUFACTURER_CODE   MdAu
    PLUGIN_CODE                Mdk1
    FORMATS                    ${MD_FORMATS}
    IS_SYNTH                   FALSE
    NEEDS_MIDI_INPUT           FALSE
    NEEDS_MIDI_OUTPUT          FALSE
    IS_MIDI_EFFECT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD    FALSE
    VST3_CATEGORIES            Fx Dynamics Mastering
    AU_MAIN_TYPE               kAudioUnitType_Effect)

target_sources(MasterDesk PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/PresetManager.cpp
    src/ui/MasterDeskLookAndFeel.cpp
    src/ui/MeterPanel.cpp)

target_compile_definitions(MasterDesk PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    MASTERDESK_USE_OPENGL=$<BOOL:${MASTERDESK_ENABLE_OPENGL}>)

set(MD_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_dsp)
if(MASTERDESK_ENABLE_OPENGL)
    list(APPEND MD_JUCE_MODULES juce::juce_opengl)
endif()

target_link_libraries(MasterDesk
    PRIVATE ${MD_JUCE_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# ------------------------------------------------------------ CLAP (opt) ----
if(MASTERDESK_BUILD_CLAP)
    # pinned past 0.26.0: this revision adds the JUCE >= 8.0.11 include guards
    # (juce_audio_processors_headless) that JUCE 8.0.14 requires
    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        16e9d4ca7b1e86c76e04584b2c08e85a764bcda8)
    FetchContent_MakeAvailable(clap-juce-extensions)
    clap_juce_extensions_plugin(TARGET MasterDesk
        CLAP_ID       "com.masterdeskaudio.masterdesk"
        CLAP_FEATURES audio-effect mastering)
endif()

# ----------------------------------------------------------------- tests ----
if(MASTERDESK_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(MasterDeskTests PRODUCT_NAME "MasterDeskTests")
    target_sources(MasterDeskTests PRIVATE tests/DspTests.cpp)
    target_compile_definitions(MasterDeskTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(MasterDeskTests
        PRIVATE juce::juce_dsp
        PUBLIC  juce::juce_recommended_config_flags)

    add_test(NAME dsp COMMAND MasterDeskTests)
endif()
