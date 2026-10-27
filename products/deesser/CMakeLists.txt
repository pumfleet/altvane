# Altvane De-Esser: a de-esser with vocal-aware detection and a phase-transparent split band

altvane_product_version(deesser 0.1.0 deesser_version)

altvane_add_plugin(AltvaneDeEsser
        PRODUCT_NAME "Altvane De-Esser"
        CODE deesser
        PLUGIN_CODE AvDs
        VERSION ${deesser_version}
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES Fx Dynamics
        AAX_CATEGORY AAX_ePlugInCategory_Dynamics
        LV2_TAGS dynamics
        LINK_LIBRARIES pffft
)

if (ALTVANE_BUILD_TESTS)
    altvane_add_tests(AltvaneDeEsser "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif ()

if (ALTVANE_BUILD_TOOLS)
    # offline renderer: processes a WAV file (or a synthetic voice), for listening and benchmarks
    add_executable(AltvaneDeEsserRender tools/render/main.cpp)
    altvane_link_plugin_code(AltvaneDeEsserRender AltvaneDeEsser)
    # headless snapshot renderer for visual review
    add_executable(AltvaneDeEsserSnapshot tools/snapshot/main.cpp)
    altvane_link_plugin_code(AltvaneDeEsserSnapshot AltvaneDeEsser)
endif ()
