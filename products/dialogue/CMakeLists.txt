# Altvane Dialogue: neural dialogue isolation (voice, ambience and voice reverb, each with its own level)

altvane_product_version(dialogue 0.1.0 dialogue_version)

# ---- ONNX Runtime: a static, CPU-only build with just the operators our models use ----
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/PLATFORMS" dialogue_platforms REGEX "^[^#]")
if (NOT ALTVANE_PLATFORM IN_LIST dialogue_platforms)
    message(FATAL_ERROR "Altvane Dialogue does not build on ${ALTVANE_PLATFORM} yet (see PLATFORMS)")
endif ()
if (APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 11)
    # std::atomic::wait (the worker thread) and ONNX Runtime need macOS 11
    message(FATAL_ERROR "Altvane Dialogue needs macOS 11 or newer: configure with -DCMAKE_OSX_DEPLOYMENT_TARGET=11")
endif ()
if (CMAKE_OSX_ARCHITECTURES)
    set(dialogue_arch ${CMAKE_OSX_ARCHITECTURES})
else ()
    set(dialogue_arch ${CMAKE_SYSTEM_PROCESSOR})
endif ()
# MSVC's debug and release runtimes don't mix, so the Debug configuration on Windows links a Debug ONNX Runtime
# (multi-config generators get both)
get_property(dialogue_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
set(dialogue_ort_configs "")
if (MSVC AND (dialogue_multi_config OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
    list(APPEND dialogue_ort_configs Debug)
endif ()
if (NOT MSVC OR dialogue_multi_config OR NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND dialogue_ort_configs Release)
endif ()
set(dialogue_ops_config "${CMAKE_CURRENT_SOURCE_DIR}/models/required_operators.config")
set(ALTVANE_ONNXRUNTIME_DIR "${CMAKE_SOURCE_DIR}/build/deps/onnxruntime-1.28.0-${dialogue_arch}"
        CACHE PATH "Static ONNX Runtime built by scripts/build_onnxruntime.py (a Debug one in <dir>-debug)")
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(dialogue_ort_options --cmake "${CMAKE_COMMAND}")
if (CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND dialogue_ort_options --macos-target ${CMAKE_OSX_DEPLOYMENT_TARGET})
endif ()
if (WIN32)
    set(dialogue_ort_lib onnxruntime.lib)
else ()
    set(dialogue_ort_lib libonnxruntime.a)
endif ()
add_library(altvane_onnxruntime STATIC IMPORTED GLOBAL)
foreach (config ${dialogue_ort_configs})
    if (config STREQUAL "Debug")
        set(dir "${ALTVANE_ONNXRUNTIME_DIR}-debug")
    else ()
        set(dir "${ALTVANE_ONNXRUNTIME_DIR}")
    endif ()
    # a no-op when the library is up to date
    execute_process(
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/build_onnxruntime.py"
            ${dialogue_arch} "${dialogue_ops_config}" "${dir}" --config ${config} ${dialogue_ort_options}
            RESULT_VARIABLE dialogue_ort_result)
    if (NOT dialogue_ort_result EQUAL 0)
        message(FATAL_ERROR "Building ONNX Runtime failed; run scripts/build_onnxruntime.py by hand to see why")
    endif ()
    # with both, the Debug archive is only for the Debug configuration
    if (config STREQUAL "Debug" AND "Release" IN_LIST dialogue_ort_configs)
        set_target_properties(altvane_onnxruntime PROPERTIES IMPORTED_LOCATION_DEBUG "${dir}/lib/${dialogue_ort_lib}")
    else ()
        set_target_properties(altvane_onnxruntime PROPERTIES IMPORTED_LOCATION "${dir}/lib/${dialogue_ort_lib}")
    endif ()
    set_target_properties(altvane_onnxruntime PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${dir}/include")
endforeach ()
if (WIN32)
    set_target_properties(altvane_onnxruntime PROPERTIES INTERFACE_LINK_LIBRARIES "$<$<CONFIG:Debug>:dbghelp>")
elseif (APPLE)
    set_target_properties(altvane_onnxruntime PROPERTIES INTERFACE_LINK_LIBRARIES "-framework Foundation")
else ()
    find_package(Threads REQUIRED)
    set_target_properties(altvane_onnxruntime PROPERTIES INTERFACE_LINK_LIBRARIES "Threads::Threads;${CMAKE_DL_LIBS}")
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
