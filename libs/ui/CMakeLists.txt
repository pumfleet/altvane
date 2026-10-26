# altvane_ui: the Altvane design system shared by every product's editor

# fonts and other art, embedded in the binary
file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
juce_add_binary_data(altvane_ui_assets SOURCES ${asset_files})
# Required for Linux happiness: https://forum.juce.com/t/loading-pytorch-model-using-binarydata/39997/2
set_target_properties(altvane_ui_assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE FOLDER "Targets")

add_library(altvane_ui INTERFACE)
target_link_libraries(altvane_ui INTERFACE altvane_ui_assets)
