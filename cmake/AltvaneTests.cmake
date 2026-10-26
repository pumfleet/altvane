# Catch2 unit tests, one executable per product.
#
#   altvane_add_tests(<plugin-target> <test-dir>)
#
# builds every .cpp under <test-dir> into <plugin-target>Tests, linked against the product's core library, and
# registers the cases with CTest.

# Required for ctest (which is just an easier way to run in cross-platform CI)
# See: https://github.com/catchorg/Catch2/issues/2026
enable_testing()
set(CTEST_OUTPUT_ON_FAILURE ON)
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)

include(FetchContent)
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_PROGRESS TRUE
        GIT_SHALLOW TRUE
        GIT_TAG v3.4.0)
FetchContent_MakeAvailable(Catch2)
include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

function(altvane_add_tests plugin_target test_dir)
    file(GLOB_RECURSE test_files CONFIGURE_DEPENDS "${test_dir}/*.cpp" "${test_dir}/*.h" "${test_dir}/*.hpp")
    source_group(TREE "${test_dir}" PREFIX "" FILES ${test_files})

    set(tests "${plugin_target}Tests")
    add_executable(${tests} ${test_files})
    target_compile_features(${tests} PRIVATE cxx_std_20)
    target_include_directories(${tests} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")

    # the plugin target's JUCE definitions (JucePlugin_Name etc.) so tests see the same configuration
    target_compile_definitions(${tests} PRIVATE $<TARGET_PROPERTY:${plugin_target},COMPILE_DEFINITIONS>)
    target_link_libraries(${tests} PRIVATE ${plugin_target}_Core Catch2::Catch2WithMain)
    target_compile_definitions(${tests} PUBLIC
            JUCE_MODAL_LOOPS_PERMITTED=1 # let us run Message Manager in tests
            RUN_PAMPLEJUCE_TESTS=1
    )
    set_target_properties(${tests} PROPERTIES XCODE_GENERATE_SCHEME ON)

    catch_discover_tests(${tests})
endfunction()
