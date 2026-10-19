cmake_minimum_required(VERSION 3.22)
project(scaredycat VERSION 0.2.0 LANGUAGES C CXX)

include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.16-security GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)
FetchContent_Declare(cat_ui GIT_REPOSITORY https://github.com/SpeakingColors/cat-ui GIT_TAG main)
FetchContent_MakeAvailable(cat_ui)

juce_add_plugin(ScaredyCat
    COMPANY_NAME "Studio 87"
    PLUGIN_MANUFACTURER_CODE Spco
    PLUGIN_CODE Scrd
    FORMATS VST3 Standalone
    PRODUCT_NAME "Scaredy Cat"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE)
target_sources(ScaredyCat PRIVATE src/common/FxBase.cpp src/common/FxEditor.cpp src/Plugins.cpp src/scaredy/Scaredy.cpp src/scaredy/ScaredyEditor.cpp)
target_include_directories(ScaredyCat PRIVATE src)
target_compile_definitions(ScaredyCat PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ScaredyCat PRIVATE cat_ui juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
