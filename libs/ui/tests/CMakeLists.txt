# AltvaneUITests: Catch2 tests for altvane_ui's logic (value formatting, presets, A/B, undo, the parameter editor),
# registered with CTest next to every product's tests.
# altvane_ui compiles into whatever links it, so this console app builds it, and the JUCE modules it needs, on its own
# under a stand-in plugin name: nothing it does can reach a real product's presets or settings.

juce_add_console_app(AltvaneUITests PRODUCT_NAME "Altvane UI Tests" VERSION 1.0.0)

file(GLOB_RECURSE test_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
target_sources(AltvaneUITests PRIVATE ${test_files})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "" FILES ${test_files})
target_compile_features(AltvaneUITests PRIVATE cxx_std_20)

target_compile_definitions(AltvaneUITests
        PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # the typing tests pump the message loop, which JUCE only allows with modal loops (as in the product tests)
        JUCE_MODAL_LOOPS_PERMITTED=1
        # what altvane_ui reads from a plugin's settings (juce_add_plugin, altvane_add_plugin)
        JucePlugin_Manufacturer="${ALTVANE_COMPANY_NAME}"
        JucePlugin_Name="Altvane UI Tests"
        PRODUCT_NAME_WITHOUT_VERSION="Altvane UI Tests"
        VERSION="1.0.0"
)

target_link_libraries(AltvaneUITests
        PRIVATE
        altvane_ui
        juce_audio_processors
        juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        Catch2::Catch2WithMain
)
set_target_properties(AltvaneUITests PROPERTIES FOLDER "Targets" XCODE_GENERATE_SCHEME ON)

catch_discover_tests(AltvaneUITests)
