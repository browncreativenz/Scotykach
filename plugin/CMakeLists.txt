# The Scotykach plugin: VST3, AU (macOS) and Standalone.

set(SCOTYKACH_PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SCOTYKACH_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(Scotykach
    PRODUCT_NAME                "Scotykach"
    COMPANY_NAME                "browncreativenz"
    BUNDLE_ID                   "nz.browncreative.scotykach"
    PLUGIN_MANUFACTURER_CODE    Brcn
    PLUGIN_CODE                 Scty
    FORMATS                     ${SCOTYKACH_PLUGIN_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE     # notes trigger the decks, CCs set them; makes the AU a music effect (aumf)
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Delay
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect
    COPY_PLUGIN_AFTER_BUILD     ${SCOTYKACH_COPY_PLUGIN})

target_sources(Scotykach PRIVATE PluginProcessor.cpp PluginEditor.cpp)

target_compile_definitions(Scotykach PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Scotykach
    PRIVATE
        scotykach_fw
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
