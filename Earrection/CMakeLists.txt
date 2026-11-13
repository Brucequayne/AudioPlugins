if(APPLE)
    set(EARRECTION_FORMATS VST3 AU)
else()
    set(EARRECTION_FORMATS VST3)
endif()

juce_add_plugin(Earrection
    COMPANY_NAME "Brucequayne"
    PLUGIN_MANUFACTURER_CODE Brqy
    PLUGIN_CODE Earr
    FORMATS ${EARRECTION_FORMATS}
    PRODUCT_NAME "Earrection"
    BUNDLE_ID com.brucequayne.earrection
    VERSION 0.1.0
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_MusicEffect)

target_sources(Earrection PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Earrection PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_XRANDR=0
    JUCE_USE_XINERAMA=0
    JUCE_USE_XCURSOR=0)

target_link_libraries(Earrection
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
