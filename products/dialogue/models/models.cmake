# The separation models, see models/README.md.
# Each is downloaded once at its pinned revision, checked against its SHA-256 and compiled into
# altvane_dialogue_models with #embed, so the plug-in carries its weights and needs no files at run time.

set(dialogue_models_dir "${CMAKE_SOURCE_DIR}/build/deps/models")
set(dialogue_dpdfnet_url "https://huggingface.co/Ceva-IP/DPDFNet/resolve/dd6818d00f50c836fed43a6243ebe49116de5964/onnx")

function(dialogue_fetch_model name sha256)
    set(file "${dialogue_models_dir}/${name}.onnx")
    if (EXISTS "${file}")
        file(SHA256 "${file}" actual)
        if (actual STREQUAL sha256)
            return()
        endif ()
    endif ()
    message(STATUS "Downloading model ${name}")
    file(DOWNLOAD "${dialogue_dpdfnet_url}/${name}.onnx" "${file}" EXPECTED_HASH SHA256=${sha256} STATUS status)
    list(GET status 0 code)
    if (NOT code EQUAL 0)
        message(FATAL_ERROR "Could not download model ${name}: ${status}")
    endif ()
endfunction()

dialogue_fetch_model(dpdfnet2_48khz_hr 7f0575a5cec0ba4ffd8f8bd657e06d007e4ccdd955d76faab922b9d3291dc14b)
dialogue_fetch_model(dpdfnet8_48khz_hr 7b3afbb260a08fe9af3d16e3bda992971be1e7e951d1dee7c2d235f5c43f5631)

add_library(altvane_dialogue_models STATIC "${CMAKE_CURRENT_LIST_DIR}/embed/model_data.cpp")
target_include_directories(altvane_dialogue_models PUBLIC "${CMAKE_CURRENT_LIST_DIR}/embed")
target_compile_options(altvane_dialogue_models PRIVATE "--embed-dir=${dialogue_models_dir}")
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/embed/model_data.cpp" PROPERTIES OBJECT_DEPENDS
        "${dialogue_models_dir}/dpdfnet2_48khz_hr.onnx;${dialogue_models_dir}/dpdfnet8_48khz_hr.onnx")
set_target_properties(altvane_dialogue_models PROPERTIES POSITION_INDEPENDENT_CODE TRUE FOLDER "Targets")
