# Altvane Limiter: true-peak limiter and loudness meter on the ZL Limiter engine

altvane_product_version(limiter 0.1.0 limiter_version)

altvane_add_plugin(AltvaneLimiter
        PRODUCT_NAME "Altvane Limiter"
        CODE limiter
        PLUGIN_CODE AvLm
        VERSION ${limiter_version}
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES Fx Dynamics
        AAX_CATEGORY AAX_ePlugInCategory_Dynamics
        LV2_TAGS limiter dynamics
        LINK_LIBRARIES hwy
)

if (ALTVANE_BUILD_TESTS)
    altvane_add_tests(AltvaneLimiter "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif ()

if (ALTVANE_BUILD_TOOLS)
    # headless snapshot renderer for visual review
    add_executable(AltvaneLimiterSnapshot tools/snapshot/main.cpp)
    altvane_link_plugin_code(AltvaneLimiterSnapshot AltvaneLimiter)
endif ()
