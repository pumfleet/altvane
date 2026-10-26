# Per-product versions from git tags.
# Each product is released with tags named <code>-vX.Y.Z (e.g. eq-v1.2.0); tags of other products and of the
# upstream projects we fork are ignored.
#
#   altvane_product_version(<code> <default> <out-var>)
#
# sets <out-var> to X.Y.Z from the newest matching tag reachable from HEAD, or to <default> if there is none.

find_package(Git QUIET)

function(altvane_product_version code default out_var)
    set(version "")
    if (GIT_EXECUTABLE)
        execute_process(
                COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --match "${code}-v*"
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE tag
                RESULT_VARIABLE result
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
        )
        if (result EQUAL 0)
            string(REGEX REPLACE "^${code}-v" "" version "${tag}")
        endif ()
    endif ()
    if (version STREQUAL "")
        set(version "${default}")
        message(STATUS "${code}: no ${code}-v* tag found, using version ${version}")
    else ()
        message(STATUS "${code}: version ${version}")
    endif ()
    set(${out_var} "${version}" PARENT_SCOPE)
endfunction()
