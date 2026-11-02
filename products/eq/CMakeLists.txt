# Altvane EQ: parametric and dynamic equalizer on the ZL Equalizer 2 engine

# number of bands compiled into the engine
set(ALTVANE_EQ_BAND_NUM 24 CACHE STRING "Number of Altvane EQ bands")
message(STATUS "Altvane EQ bands: ${ALTVANE_EQ_BAND_NUM}")

altvane_product_version(eq 0.1.0 eq_version)

altvane_add_plugin(AltvaneEQ
        PRODUCT_NAME "Altvane EQ"
        CODE eq
        PLUGIN_CODE AvEq
        VERSION ${eq_version}
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES EQ
        AAX_CATEGORY AAX_ePlugInCategory_EQ
        LV2_TAGS eq paraeq
        LINK_LIBRARIES nlopt hwy
)
target_compile_definitions(AltvaneEQ_Core INTERFACE ZL_EQ_BAND_NUM=${ALTVANE_EQ_BAND_NUM})

if (ALTVANE_BUILD_TESTS)
    altvane_add_tests(AltvaneEQ "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    # ZL Equalizer 2 sessions and presets the state tests load
    target_compile_definitions(AltvaneEQTests PRIVATE
            ALTVANE_EQ_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
endif ()

if (ALTVANE_BUILD_TOOLS)
    # headless snapshot renderer for visual review
    add_executable(AltvaneEQSnapshot tools/snapshot/main.cpp)
    altvane_link_plugin_code(AltvaneEQSnapshot AltvaneEQ)
endif ()
