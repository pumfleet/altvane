# Altvane Dialogue: neural dialogue isolation (voice, ambience and voice reverb, each with its own level)

altvane_product_version(dialogue 0.1.0 dialogue_version)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/PLATFORMS" dialogue_platforms REGEX "^[^#]")
if (NOT ALTVANE_PLATFORM IN_LIST dialogue_platforms)
    message(FATAL_ERROR "Altvane Dialogue does not build on ${ALTVANE_PLATFORM} yet (see PLATFORMS)")
endif ()
if (APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 11)
    # the worker thread waits with std::atomic::wait, which macOS has from 11
    message(FATAL_ERROR "Altvane Dialogue needs macOS 11 or newer: configure with -DCMAKE_OSX_DEPLOYMENT_TARGET=11")
endif ()

# the network's large matrix products go to the platform BLAS where there is one (dsp/kernels.cpp)
set(dialogue_blas "")
if (APPLE)
    set(dialogue_blas "-framework Accelerate")
endif ()

# ---- the models, downloaded at a pinned revision, checked, and embedded in the plug-in ----
include(models/models.cmake)

altvane_add_plugin(AltvaneDialogue
        PRODUCT_NAME "Altvane Dialogue"
        CODE dialogue
        PLUGIN_CODE AvDl
        VERSION ${dialogue_version}
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES Fx Restoration
        AAX_CATEGORY AAX_ePlugInCategory_NoiseReduction
        LV2_TAGS util
        LINK_LIBRARIES altvane_dialogue_models pffft ${dialogue_blas}
)

if (ALTVANE_BUILD_TESTS)
    altvane_add_tests(AltvaneDialogue "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif ()

if (ALTVANE_BUILD_TOOLS)
    # offline renderer: writes the stems and the mix of a WAV file, for listening and benchmarks
    add_executable(AltvaneDialogueRender tools/render/main.cpp)
    altvane_link_plugin_code(AltvaneDialogueRender AltvaneDialogue)
    # headless snapshot renderer for visual review
    add_executable(AltvaneDialogueSnapshot tools/snapshot/main.cpp)
    altvane_link_plugin_code(AltvaneDialogueSnapshot AltvaneDialogue)
endif ()
