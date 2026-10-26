# Declares an Altvane plugin. Brand-wide identity lives here once; each product only states what makes it
# different.
#
#   altvane_add_plugin(<target>
#           PRODUCT_NAME <name shown in DAWs, e.g. "Altvane EQ">
#           CODE <short product code, e.g. eq: used for tags, bundle ids, the CI env file>
#           PLUGIN_CODE <unique four-character id, at least one upper-case letter>
#           VERSION <X.Y.Z>
#           [AU_MAIN_TYPE <type>] [VST3_CATEGORIES <cat>...] [AAX_CATEGORY <cat>] [LV2_TAGS <tag>...]
#           [LINK_LIBRARIES <lib>...])
#
# It expects the product's code in ./source, its art in ./packaging (icon.png etc.) and creates
#   <target>              the JUCE plugin (and <target>_VST3, _AU, ... per format)
#   <target>_Core         an INTERFACE library with the product's sources and dependencies, shared by the plugin,
#                         its tests and its tools without ODR trouble
#   ${CMAKE_BINARY_DIR}/<code>.env   names and paths the packaging scripts and CI read

set(ALTVANE_COMPANY_NAME "Altvane")
set(ALTVANE_MANUFACTURER_CODE "Altv")
set(ALTVANE_BUNDLE_ID_PREFIX "com.altvane")
set(ALTVANE_LV2_URI_PREFIX "https://altvane.com/plugins")

function(altvane_add_plugin target)
    set(one_value PRODUCT_NAME CODE PLUGIN_CODE VERSION AU_MAIN_TYPE AAX_CATEGORY)
    set(multi_value VST3_CATEGORIES LV2_TAGS LINK_LIBRARIES)
    cmake_parse_arguments(ARG "" "${one_value}" "${multi_value}" ${ARGN})
    foreach (required PRODUCT_NAME CODE PLUGIN_CODE VERSION)
        if (NOT ARG_${required})
            message(FATAL_ERROR "altvane_add_plugin(${target}): ${required} is required")
        endif ()
    endforeach ()
    if (NOT ARG_AU_MAIN_TYPE)
        set(ARG_AU_MAIN_TYPE kAudioUnitType_Effect)
    endif ()
    if (NOT ARG_VST3_CATEGORIES)
        set(ARG_VST3_CATEGORIES Fx)
    endif ()
    if (NOT ARG_AAX_CATEGORY)
        set(ARG_AAX_CATEGORY AAX_ePlugInCategory_None)
    endif ()

    set(product_dir "${CMAKE_CURRENT_SOURCE_DIR}")

    # See `docs/CMake API.md` in the JUCE repo for all config options
    juce_add_plugin(${target}
            VERSION "${ARG_VERSION}"
            ICON_BIG "${product_dir}/packaging/icon.png"
            COMPANY_NAME "${ALTVANE_COMPANY_NAME}"
            BUNDLE_ID "${ALTVANE_BUNDLE_ID_PREFIX}.${ARG_CODE}"

            # On macOS, plugins are copied to ~/Library/Audio/Plug-Ins/
            COPY_PLUGIN_AFTER_BUILD ${ALTVANE_COPY_PLUGIN}
            AAX_COPY_DIR "${JUCE_AAX_COPY_DIR}"

            PLUGIN_MANUFACTURER_CODE "${ALTVANE_MANUFACTURER_CODE}"
            PLUGIN_CODE "${ARG_PLUGIN_CODE}"
            FORMATS ${ALTVANE_FORMATS}
            LV2URI "${ALTVANE_LV2_URI_PREFIX}/${ARG_CODE}"

            IS_SYNTH FALSE
            NEEDS_MIDI_INPUT FALSE
            NEEDS_MIDI_OUTPUT FALSE

            AU_MAIN_TYPE ${ARG_AU_MAIN_TYPE}
            VST3_CATEGORIES ${ARG_VST3_CATEGORIES}
            AAX_CATEGORY ${ARG_AAX_CATEGORY}

            MICROPHONE_PERMISSION_ENABLED TRUE
            MICROPHONE_PERMISSION_TEXT "${ARG_PRODUCT_NAME} Standalone needs access to your audio interface"
            HARDENED_RUNTIME_ENABLED TRUE
            HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input

            PRODUCT_NAME "${ARG_PRODUCT_NAME}"
    )

    # On Linux, JUCE adds -Wl,--no-undefined by default, which stops shared libraries (like VST3) from having
    # unresolved sanitizer symbols. Remove it with sanitizers on: the host provides the sanitizer runtime.
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND (WITH_ADDRESS_SANITIZER OR WITH_THREAD_SANITIZER))
        foreach (format_target "${target}_VST3" "${target}_LV2" "${target}_Standalone" "${target}_SharedCode")
            if (TARGET ${format_target})
                get_target_property(current_opts ${format_target} LINK_OPTIONS)
                if (current_opts)
                    list(REMOVE_ITEM current_opts "-Wl,--no-undefined")
                    set_target_properties(${format_target} PROPERTIES LINK_OPTIONS "${current_opts}")
                endif ()
            endif ()
        endforeach ()
    endif ()

    # LV2 tags
    if ("LV2" IN_LIST ALTVANE_FORMATS AND ARG_LV2_TAGS)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_custom_command(TARGET ${target}_LV2 POST_BUILD
                COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/packaging/patch_lv2.py"
                "$<TARGET_FILE_DIR:${target}_LV2>" ${ARG_LV2_TAGS}
                COMMENT "Patching LV2 tags: ${ARG_LV2_TAGS}..."
                VERBATIM
        )
    endif ()

    # ---- <target>_Core: the product's code, shared by the plugin, its tests and tools ----
    set(core ${target}_Core)
    add_library(${core} INTERFACE)
    file(GLOB_RECURSE source_files CONFIGURE_DEPENDS "${product_dir}/source/*.cpp" "${product_dir}/source/*.hpp")
    target_sources(${core} INTERFACE ${source_files})
    source_group(TREE "${product_dir}/source" PREFIX "" FILES ${source_files})
    target_compile_features(${core} INTERFACE cxx_std_20)

    target_compile_definitions(${core}
            INTERFACE
            JUCE_WEB_BROWSER=0  # If you set this to 1, add `NEEDS_WEB_BROWSER TRUE` to juce_add_plugin
            JUCE_USE_CURL=0     # If you set this to 1, add `NEEDS_CURL TRUE` to juce_add_plugin
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_USE_FLAC=0
            JUCE_USE_OGGVORBIS=0
            JUCE_USE_WEBP=0
            JUCE_SILENCE_XCODE_15_LINKER_WARNING=1

            # lets the app know if we're Debug or Release
            CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
            VERSION="${ARG_VERSION}"
            # JucePlugin_Name doesn't use the nicer PRODUCT_NAME
            PRODUCT_NAME_WITHOUT_VERSION="${ARG_PRODUCT_NAME}"
    )

    if (MSVC)
        # fast math and better simd support in RELEASE
        # https://learn.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior?view=msvc-170#fast
        target_compile_options(${core} INTERFACE $<$<CONFIG:RELEASE>:/fp:precise> /Zc:__cplusplus)
    else ()
        # See the implications here: https://stackoverflow.com/q/45685487
        target_compile_options(${core} INTERFACE
                $<$<CONFIG:RELEASE>:-O3 -ffp-contract=fast -fno-signed-zeros -freciprocal-math>
                $<$<CONFIG:RelWithDebInfo>:-O3 -ffp-contract=fast -fno-signed-zeros -freciprocal-math>)
    endif ()
    target_compile_options(${core} INTERFACE ${ALTVANE_SIMD_FLAGS})
    target_link_options(${core} INTERFACE ${ALTVANE_SIMD_FLAGS})
    target_compile_definitions(${core} INTERFACE HWY_COMPILE_ONLY_STATIC)

    # JUCE modules have INTERFACE visibility here so the plugin, tests and tools all get them
    target_link_libraries(${core}
            INTERFACE
            altvane_ui
            juce_audio_utils
            juce_audio_processors
            juce_gui_basics
            juce_gui_extra
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
            ${ARG_LINK_LIBRARIES}
    )

    target_link_libraries(${target} PRIVATE ${core})

    # ---- IDE tidiness ----
    foreach (format ${ALTVANE_FORMATS} "All")
        if (TARGET ${target}_${format})
            set_target_properties(${target}_${format} PROPERTIES FOLDER "Targets/${target}" XCODE_GENERATE_SCHEME ON)
        endif ()
    endforeach ()

    # ---- names and paths for packaging and CI ----
    file(RELATIVE_PATH binary_dir "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
    set(artifacts "${binary_dir}/${target}_artefacts/${CMAKE_BUILD_TYPE}")
    set(env_file "${CMAKE_BINARY_DIR}/${ARG_CODE}.env")
    set(env "")
    string(APPEND env "PROJECT_NAME=${target}\n")
    string(APPEND env "PRODUCT_NAME=${ARG_PRODUCT_NAME}\n")
    string(APPEND env "PRODUCT_CODE=${ARG_CODE}\n")
    string(APPEND env "PRODUCT_DIR=${product_dir}\n")
    string(APPEND env "VERSION=${ARG_VERSION}\n")
    string(APPEND env "BUNDLE_ID=${ALTVANE_BUNDLE_ID_PREFIX}.${ARG_CODE}\n")
    string(APPEND env "COMPANY_NAME=${ALTVANE_COMPANY_NAME}\n")
    string(APPEND env "ARTIFACTS_PATH=${artifacts}\n")
    if ("VST3" IN_LIST ALTVANE_FORMATS)
        string(APPEND env "VST3_PATH=${artifacts}/VST3/${ARG_PRODUCT_NAME}.vst3\n")
    endif ()
    if ("AU" IN_LIST ALTVANE_FORMATS)
        string(APPEND env "AU_PATH=${artifacts}/AU/${ARG_PRODUCT_NAME}.component\n")
    endif ()
    if ("LV2" IN_LIST ALTVANE_FORMATS)
        string(APPEND env "LV2_PATH=${artifacts}/LV2/${ARG_PRODUCT_NAME}.lv2\n")
    endif ()
    if ("Standalone" IN_LIST ALTVANE_FORMATS)
        if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            string(APPEND env "Standalone_PATH=${artifacts}/Standalone/${ARG_PRODUCT_NAME}.app\n")
        elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
            string(APPEND env "Standalone_PATH=${artifacts}/Standalone/${ARG_PRODUCT_NAME}.exe\n")
        else ()
            string(APPEND env "Standalone_PATH=${artifacts}/Standalone/${ARG_PRODUCT_NAME}\n")
        endif ()
    endif ()
    file(WRITE "${env_file}" "${env}")
endfunction()
