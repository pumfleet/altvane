# Altvane Dialogue: neural dialogue isolation (voice, ambience and voice reverb, each with its own level)

altvane_product_version(dialogue 0.1.0 dialogue_version)

# ---- ONNX Runtime: a static, CPU-only build with just the operators our models use ----
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/PLATFORMS" dialogue_platforms REGEX "^[^#]")
if (NOT ALTVANE_PLATFORM IN_LIST dialogue_platforms)
    message(FATAL_ERROR "Altvane Dialogue does not build on ${ALTVANE_PLATFORM} yet (see PLATFORMS)")
endif ()
if (CMAKE_OSX_ARCHITECTURES)
    set(dialogue_arch ${CMAKE_OSX_ARCHITECTURES})
else ()
    set(dialogue_arch ${CMAKE_SYSTEM_PROCESSOR})
endif ()
set(dialogue_ops_config "${CMAKE_CURRENT_SOURCE_DIR}/models/required_operators.config")
set(ALTVANE_ONNXRUNTIME_DIR "${CMAKE_SOURCE_DIR}/build/deps/onnxruntime-1.28.0-${dialogue_arch}"
        CACHE PATH "Static ONNX Runtime built by scripts/build_onnxruntime.sh")
find_package(Python3 3.10 COMPONENTS Interpreter)
set(dialogue_ort_env "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
if (Python3_FOUND)
    list(APPEND dialogue_ort_env "PYTHON=${Python3_EXECUTABLE}")
endif ()
# a no-op when the library is up to date
execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ${dialogue_ort_env}
        "${CMAKE_SOURCE_DIR}/scripts/build_onnxruntime.sh" ${dialogue_arch} "${dialogue_ops_config}"
        "${ALTVANE_ONNXRUNTIME_DIR}"
        RESULT_VARIABLE dialogue_ort_result)
if (NOT dialogue_ort_result EQUAL 0)
    message(FATAL_ERROR "Building ONNX Runtime failed; run scripts/build_onnxruntime.sh by hand (it needs Python >= 3.10)")
endif ()
add_library(altvane_onnxruntime STATIC IMPORTED GLOBAL)
set_target_properties(altvane_onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ALTVANE_ONNXRUNTIME_DIR}/lib/libonnxruntime.a"
        INTERFACE_INCLUDE_DIRECTORIES "${ALTVANE_ONNXRUNTIME_DIR}/include"
        INTERFACE_LINK_LIBRARIES "-framework Foundation")

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
        LINK_LIBRARIES altvane_onnxruntime altvane_dialogue_models pffft
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
