# altvane_ui: the Altvane design system shared by every product's editor
# (theme tokens, fonts, look-and-feel, widgets, icons, sheets, animation and formatting helpers).
# Include as "altvane_ui/<area>/<file>.hpp"; code lives in namespace altvane::ui.

# fonts and other art, embedded in the binary under their own namespace so products can add their own
file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
juce_add_binary_data(altvane_ui_assets
        NAMESPACE AltvaneUIBinary
        HEADER_NAME AltvaneUIBinary.h
        SOURCES ${asset_files})
# Required for Linux happiness: https://forum.juce.com/t/loading-pytorch-model-using-binarydata/39997/2
set_target_properties(altvane_ui_assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE FOLDER "Targets")

# an INTERFACE library: the sources compile into each plugin, test and tool with that target's JUCE configuration
add_library(altvane_ui INTERFACE)
file(GLOB_RECURSE ui_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/source/*.hpp")
target_sources(altvane_ui INTERFACE ${ui_files})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/source" PREFIX "" FILES ${ui_files})
target_include_directories(altvane_ui INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/source")
target_compile_features(altvane_ui INTERFACE cxx_std_20)
target_link_libraries(altvane_ui INTERFACE altvane_ui_assets juce_gui_basics juce_audio_processors)
