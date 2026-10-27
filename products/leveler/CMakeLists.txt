# Altvane Leveler: an automatic vocal rider, with lookahead, voicing detection and a music key input

altvane_product_version(leveler 0.1.0 leveler_version)

altvane_add_plugin(AltvaneLeveler
        PRODUCT_NAME "Altvane Leveler"
        CODE leveler
        PLUGIN_CODE AvLv
        VERSION ${leveler_version}
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES Fx Dynamics
        AAX_CATEGORY AAX_ePlugInCategory_Dynamics
        LV2_TAGS dynamics amplifier
)

if (ALTVANE_BUILD_TESTS)
    altvane_add_tests(AltvaneLeveler "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif ()

if (ALTVANE_BUILD_TOOLS)
    # offline renderer: levels a WAV file and writes the gain curve, for listening and benchmarks
    add_executable(AltvaneLevelerRender tools/render/main.cpp)
    altvane_link_plugin_code(AltvaneLevelerRender AltvaneLeveler)
    # headless snapshot renderer for visual review
    add_executable(AltvaneLevelerSnapshot tools/snapshot/main.cpp)
    altvane_link_plugin_code(AltvaneLevelerSnapshot AltvaneLeveler)
endif ()
