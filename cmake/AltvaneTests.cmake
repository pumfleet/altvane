# Catch2 unit tests, one executable per product.
#
#   altvane_add_tests(<plugin-target> <test-dir>)
#
# builds every .cpp under <test-dir> into <plugin-target>Tests, linked against the plugin's compiled code (see
# altvane_link_plugin_code), and registers the cases with CTest.

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
    altvane_link_plugin_code(${tests} ${plugin_target})
    target_link_libraries(${tests} PRIVATE Catch2::Catch2WithMain)
    set_target_properties(${tests} PROPERTIES XCODE_GENERATE_SCHEME ON)

    catch_discover_tests(${tests})
endfunction()
