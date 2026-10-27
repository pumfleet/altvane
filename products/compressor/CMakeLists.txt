# Altvane Compressor: downward and upward compressor and expander on the ZL Compressor engine

# highest oversampling rate compiled into the engine (0: Off, 1: 2x, 2: 4x, 3: 8x, 4: 16x, 5: 32x, 6: 64x)
set(ALTVANE_COMPRESSOR_MAX_OVERSAMPLE 3 CACHE STRING "Highest Altvane Compressor oversampling rate (power of two)")
message(STATUS "Altvane Compressor oversampling: up to ${ALTVANE_COMPRESSOR_MAX_OVERSAMPLE}")

altvane_product_version(compressor 0.1.0 compressor_version)

altvane_add_plugin(AltvaneCompressor
        PRODUCT_NAME "Altvane Compressor"
        CODE compressor
        PLUGIN_CODE AvCp
        VERSION ${compressor_version}
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES Fx Dynamics
        AAX_CATEGORY AAX_ePlugInCategory_Dynamics
        LV2_TAGS compressor dynamics expander
        LINK_LIBRARIES hwy
)
target_compile_definitions(AltvaneCompressor_Core INTERFACE ZL_MAX_OVERSAMPLE_RATE=${ALTVANE_COMPRESSOR_MAX_OVERSAMPLE})

if (ALTVANE_BUILD_TESTS)
    altvane_add_tests(AltvaneCompressor "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif ()
