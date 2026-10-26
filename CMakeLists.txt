# Altvane: one build for every Altvane plugin.
# Shared dependencies (JUCE, highway, nlopt) and libs/ live here; each product under products/ is added with
# add_subdirectory and declares its plugin with altvane_add_plugin (cmake/AltvanePlugin.cmake).

# 3.24.1 is bundled in Visual Studio 2022 v17.4
# 3.24.1 is also bundled in CLion as of 2023
cmake_minimum_required(VERSION 3.24.1)
set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Configures universal binaries and decides which version of macOS to support
include(PamplejuceMacOS)

project(Altvane LANGUAGES C CXX)

# ---- build options ----

# products to build, by directory name under products/ (default: all of them)
file(GLOB _altvane_product_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/products" "${CMAKE_CURRENT_SOURCE_DIR}/products/*")
set(_altvane_all_products "")
foreach (dir ${_altvane_product_dirs})
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/products/${dir}/CMakeLists.txt")
        list(APPEND _altvane_all_products "${dir}")
    endif ()
endforeach ()
set(ALTVANE_PRODUCTS "${_altvane_all_products}" CACHE STRING "Products to build (directory names under products/)")
message(STATUS "Altvane products: ${ALTVANE_PRODUCTS}")

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_altvane_default_formats VST3 AU Standalone)
else ()
    set(_altvane_default_formats VST3 LV2 Standalone)
endif ()
set(ALTVANE_FORMATS "${_altvane_default_formats}" CACHE STRING "Plugin formats to build")
message(STATUS "Formats are ${ALTVANE_FORMATS}")

option(ALTVANE_COPY_PLUGIN "Copy plugins to the system plugin folders after building" ON)
option(ALTVANE_BUILD_TESTS "Build the unit tests of every product" OFF)
option(ALTVANE_BUILD_TOOLS "Build developer tools (snapshot renderers, icon generator)" OFF)

if ("AAX" IN_LIST ALTVANE_FORMATS)
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if (DEFINED ENV{GITHUB_TOKEN})
            set(JUCE_AAX_COPY_DIR "/Users/runner/Library/Audio/Plug-Ins/AAX")
        else ()
            set(JUCE_AAX_COPY_DIR "/Library/Application Support/Avid/Audio/Plug-Ins")
        endif ()
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(JUCE_AAX_COPY_DIR "C:/Program Files/Common Files/Avid/Audio/Plug-Ins")
    endif ()
endif ()

# ---- global settings ----

# Sanitizer support, off unless WITH_ADDRESS_SANITIZER / WITH_THREAD_SANITIZER are set
include(Sanitizer)

# Couple tweaks that IMO should be JUCE defaults
include(JUCEDefaults)

# ---- third-party dependencies ----

# JUCE is a submodule in third_party/JUCE
# Locally, you must run `git submodule update --init --recursive` once
add_subdirectory(third_party/JUCE)

# nlopt
set(BUILD_SHARED_LIBS OFF)
set(DISABLE_FP_CONTRACT OFF)
add_subdirectory(third_party/nlopt EXCLUDE_FROM_ALL)

# highway, statically dispatched to one SIMD target
set(ALTVANE_SIMD_TARGET "NONE" CACHE STRING "Target architecture for static dispatch: SSE2/SSE4/AVX2/NEON/LSX/LASX")
if (ALTVANE_SIMD_TARGET STREQUAL "NONE")
    message(FATAL_ERROR "ALTVANE_SIMD_TARGET must be set: SSE2/SSE4/AVX2/NEON/LSX/LASX.")
endif ()
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "Disable highway examples" FORCE)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "Disable highway tests" FORCE)
set(HWY_COMPILE_ONLY_STATIC ON CACHE BOOL "Force highway static dispatch" FORCE)
if (ALTVANE_SIMD_TARGET STREQUAL "NEON")
    set(ALTVANE_SIMD_FLAGS "-march=armv8-a+simd")
elseif (ALTVANE_SIMD_TARGET STREQUAL "SSE2")
    set(ALTVANE_SIMD_FLAGS "-march=x86-64")
elseif (ALTVANE_SIMD_TARGET STREQUAL "SSE4")
    set(ALTVANE_SIMD_FLAGS "-march=x86-64-v2" "-maes" "-mpclmul")
elseif (ALTVANE_SIMD_TARGET STREQUAL "AVX2")
    set(ALTVANE_SIMD_FLAGS "-march=x86-64-v3" "-maes" "-mpclmul")
elseif (ALTVANE_SIMD_TARGET STREQUAL "LSX")
    set(ALTVANE_SIMD_FLAGS "-mlsx")
elseif (ALTVANE_SIMD_TARGET STREQUAL "LASX")
    set(ALTVANE_SIMD_FLAGS "-mlasx")
else ()
    message(FATAL_ERROR "Unsupported ALTVANE_SIMD_TARGET chosen: ${ALTVANE_SIMD_TARGET}")
endif ()
message(STATUS "SIMD target is ${ALTVANE_SIMD_TARGET}")
add_compile_definitions(HWY_COMPILE_ONLY_STATIC)
add_compile_options(${ALTVANE_SIMD_FLAGS})
add_subdirectory(third_party/highway EXCLUDE_FROM_ALL)

# r8brain: sample rate conversion, and the PFFFT it bundles for FFT sizes that are not powers of two
add_library(r8brain STATIC EXCLUDE_FROM_ALL third_party/r8brain/r8bbase.cpp third_party/r8brain/pffft.cpp)
target_include_directories(r8brain SYSTEM PUBLIC third_party/r8brain)
set_target_properties(r8brain PROPERTIES POSITION_INDEPENDENT_CODE TRUE FOLDER "Targets")

# ---- Altvane ----

include(AltvaneVersion)
include(AltvanePlugin)
if (ALTVANE_BUILD_TESTS)
    include(AltvaneTests)
endif ()

add_subdirectory(libs/ui)

foreach (product ${ALTVANE_PRODUCTS})
    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/products/${product}/CMakeLists.txt")
        message(FATAL_ERROR "Unknown product '${product}': products/${product}/CMakeLists.txt does not exist")
    endif ()
    add_subdirectory("products/${product}")
endforeach ()

if (ALTVANE_BUILD_TOOLS)
    add_subdirectory(tools/icons)
endif ()
