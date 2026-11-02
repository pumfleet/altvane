# altvane_testing: what every product's unit tests share
# (alloc_counter, which holds the audio thread to no allocations, and the Catch2 listener that keeps JUCE initialised
# for the whole run). altvane_add_tests (cmake/AltvaneTests.cmake) links it into each test binary.
# Include as "altvane_testing/<file>.hpp".

# an INTERFACE library: the sources compile into each test binary, so the listener's registration and the operator
# new replacement are always linked in (a static library would drop them, as nothing refers to them); JUCE's headers
# and configuration come from the plugin the tests link (altvane_link_plugin_code)
add_library(altvane_testing INTERFACE)
file(GLOB_RECURSE testing_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/source/*.hpp")
target_sources(altvane_testing INTERFACE ${testing_files})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/source" PREFIX "" FILES ${testing_files})
target_include_directories(altvane_testing INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/source")
target_compile_features(altvane_testing INTERFACE cxx_std_20)
target_link_libraries(altvane_testing INTERFACE Catch2::Catch2)
