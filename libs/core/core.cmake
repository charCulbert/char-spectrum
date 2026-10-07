# What every plugin here builds on: the CLAP SDK, clap-wrapper (VST3, AU,
# standalone and WCLAP from the one CLAP plugin), the message codec its
# interface (and its state) use, and the loader for the files it ships.
#
#   include(libs/core/core.cmake)
#   core_add_to(<impl target>)                    codec and resource loader
#   core_ship_resources(<plugin name> <folder>)   copies <folder> into every format
#   core_ship_resources(<plugin name> "")         no files: signs and packages only
include_guard(DIRECTORY)
include(FetchContent)

set(CORE_DIR "${CMAKE_CURRENT_LIST_DIR}")

FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG 1.2.10
    GIT_SHALLOW TRUE)
# clap-wrapper downloads the VST3 and AudioUnit SDKs it needs.
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE)
FetchContent_Declare(clap-wrapper
    GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
    GIT_TAG 2bc329c774b4584918c2150dda918afd5e6b2b87)
FetchContent_MakeAvailable(clap clap-wrapper)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI" AND TARGET clap-wrapper-shared-detail)
    # Only the native wrappers use this library, and under WASI the pinned
    # clap-wrapper gives it an empty define (-D=1) that fails to compile.
    set_target_properties(clap-wrapper-shared-detail PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# The implementation library every format links: the plugin's own sources are
# added by the caller. Headers include libs by folder: "core/messages.h".
function(core_add_to target)
    target_sources(${target} PRIVATE "${CORE_DIR}/messages.cpp" "${CORE_DIR}/resources.cpp")
    target_include_directories(${target} PUBLIC "${CORE_DIR}/..")
    target_link_libraries(${target} PUBLIC clap)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS) # keep the portable C string functions
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()

# Ships <folder> (the plugin's resources: a web page in page/, fonts, images,
# sounds...) with each format of the plugin make_clapfirst_plugins made
# (TARGET_NAME <name>), where core/resources.cpp reads it:
#   macOS and VST3 bundles   <bundle>/Contents/Resources/
#   loose binaries           <binary>.resources/ (Windows and Linux CLAPs)
#   WCLAP                    <name>.wclap.tar.gz: module.wasm and resources/
# The project's LICENSE and THIRD_PARTY_NOTICES.md, where they exist, go with
# it (at the WCLAP's root), and so does THIRD_PARTY_LICENSES/: the licence of
# every library the build downloaded and every git submodule (see
# core_third_party_licenses), plus the WASI SDK's runtime in a WCLAP. The copy
# runs on every build, so edits to the folder need no relink. Pass DEPENDS
# <target> when a build step assembles the folder, and LICENSES <name>=<file>
# for a library whose licence file isn't at the top of its folder.
function(core_ship_resources name folder)
    cmake_parse_arguments(ARG "" "" "DEPENDS;LICENSES" ${ARGN})
    set(notices "")
    foreach(file LICENSE THIRD_PARTY_NOTICES.md)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
            list(APPEND notices "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
        endif()
    endforeach()
    core_third_party_licenses(licenses ${ARG_LICENSES})
    foreach(format clap vst3 auv2 auv3 standalone aax)
        set(target ${name}_${format})
        if(NOT TARGET ${target})
            continue()
        endif()
        if(APPLE OR format MATCHES "vst3|aax")
            set(destination "$<TARGET_FILE_DIR:${target}>/../Resources")
        else()
            # next to a loose binary (TARGET_FILE here would make the target depend on itself)
            set(destination "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:${target}>.resources")
        endif()
        set(copyFolder "")
        if(folder)
            set(copyFolder COMMAND ${CMAKE_COMMAND} -E copy_directory "${folder}" "${destination}")
        endif()
        set(copyLicences COMMAND ${CMAKE_COMMAND} -E copy_directory "${licenses}" "${destination}/THIRD_PARTY_LICENSES")
        if(notices)
            list(APPEND copyLicences COMMAND ${CMAKE_COMMAND} -E copy ${notices} "${destination}")
        endif()
        add_custom_target(${target}_resources
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${destination}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
            ${copyFolder}
            ${copyLicences}
            VERBATIM)
        if(ARG_DEPENDS)
            add_dependencies(${target}_resources ${ARG_DEPENDS})
        endif()
        add_dependencies(${target} ${target}_resources)
        # The linker signs only the binary; sign the bundle so its resources are
        # sealed too, or `codesign -v` (and stricter hosts) reject it. A build
        # that only copies resources changes the bundle without relinking, so
        # sign it again at the end of every build. AUv3 and AAX are signed by
        # their own build steps.
        if(APPLE AND NOT format MATCHES "auv3|aax")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>"
                VERBATIM)
            add_custom_target(${target}_sign ALL
                COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>"
                VERBATIM)
            add_dependencies(${target}_sign ${target})
        endif()
    endforeach()

    if(TARGET ${name}_wclap)
        # Browsers download the module, so leave debug info out of Release builds.
        target_link_options(${name}_wclap PRIVATE $<$<CONFIG:Release,MinSizeRel>:-Wl,--strip-debug>)
        # A WCLAP is a .tar.gz of module.wasm plus its files, at the archive root.
        set(bundle "$<TARGET_FILE_DIR:${name}_wclap>/${name}.wclap")
        set(contents module.wasm)
        set(copy "")
        if(folder)
            list(APPEND contents resources)
            set(copy COMMAND ${CMAKE_COMMAND} -E copy_directory "${folder}" "${bundle}/resources")
        endif()
        list(APPEND copy COMMAND ${CMAKE_COMMAND} -E copy_directory "${licenses}" "${bundle}/THIRD_PARTY_LICENSES")
        list(APPEND contents THIRD_PARTY_LICENSES)
        if(notices)
            list(APPEND copy COMMAND ${CMAKE_COMMAND} -E copy ${notices} "${bundle}")
            foreach(file ${notices})
                get_filename_component(file "${file}" NAME)
                list(APPEND contents "${file}")
            endforeach()
        endif()
        add_custom_target(${name}_wclap_bundle ALL
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${bundle}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${bundle}"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${name}_wclap>" "${bundle}/module.wasm"
            ${copy}
            COMMAND ${CMAKE_COMMAND} -E chdir "${bundle}"
                    ${CMAKE_COMMAND} -E tar czf "../${name}.wclap.tar.gz" --format=gnutar ${contents}
            DEPENDS ${name}_wclap
            VERBATIM)
        if(ARG_DEPENDS)
            add_dependencies(${name}_wclap_bundle ${ARG_DEPENDS})
        endif()
    endif()
endfunction()

# Gathers third-party licences into <build>/THIRD_PARTY_LICENSES/<name>.txt and
# sets <out> to that folder: the licence file at the top of every library
# FetchContent or CPM downloaded and of every git submodule (recursively), and
# in a WCLAP the WASI SDK's runtime (licenses/wasi-sdk/, for WASI SDK 33).
# <name>=<file> arguments name a licence by hand, and win over what is found;
# so do entries libraries add to the global property CORE_THIRD_PARTY_LICENSES.
# <name>= (no file) marks a download another entry covers. A library with no
# licence file stops the configure, so none ships unnoticed.
function(core_third_party_licenses out)
    set(folder "${CMAKE_BINARY_DIR}/THIRD_PARTY_LICENSES")
    file(REMOVE_RECURSE "${folder}")
    file(MAKE_DIRECTORY "${folder}")

    get_property(registered GLOBAL PROPERTY CORE_THIRD_PARTY_LICENSES)
    set(covered "")
    foreach(entry ${ARGN} ${registered})
        if(NOT entry MATCHES "^([^=]+)=(.*)$")
            message(FATAL_ERROR "LICENSES ${entry}: expected <name>=<file>")
        endif()
        list(APPEND covered "${CMAKE_MATCH_1}")
        if(CMAKE_MATCH_2 STREQUAL "")
            continue()
        endif()
        set(name "${CMAKE_MATCH_1}")
        get_filename_component(file "${CMAKE_MATCH_2}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NOT EXISTS "${file}")
            message(FATAL_ERROR "No licence file for ${name}: ${file} does not exist")
        endif()
        file(COPY_FILE "${file}" "${folder}/${name}.txt")
    endforeach()

    set(libraries "")
    file(GLOB downloads LIST_DIRECTORIES true "${CMAKE_BINARY_DIR}/_deps/*" "${CMAKE_BINARY_DIR}/cpm/*")
    foreach(path ${downloads})
        if(IS_DIRECTORY "${path}" AND NOT path MATCHES "-(build|subbuild)$")
            list(APPEND libraries "${path}")
        endif()
    endforeach()
    set(repositories "${CMAKE_CURRENT_SOURCE_DIR}")
    while(repositories)
        list(POP_FRONT repositories repository)
        if(EXISTS "${repository}/.gitmodules")
            file(STRINGS "${repository}/.gitmodules" paths REGEX "^[ \t]*path[ \t]*=")
            foreach(path ${paths})
                string(REGEX REPLACE "^[ \t]*path[ \t]*=[ \t]*" "" path "${path}")
                # An empty folder is a submodule left unchecked out: nothing of it is built.
                file(GLOB checkedOut "${repository}/${path}/*")
                if(checkedOut)
                    list(APPEND libraries "${repository}/${path}")
                    list(APPEND repositories "${repository}/${path}")
                endif()
            endforeach()
        endif()
    endwhile()

    set(missing "")
    foreach(library ${libraries})
        get_filename_component(name "${library}" NAME)
        string(REGEX REPLACE "-src$" "" name "${name}")
        if(name IN_LIST covered OR EXISTS "${folder}/${name}.txt")
            continue()
        endif()
        file(GLOB found LIST_DIRECTORIES false
            "${library}/LICENSE*" "${library}/LICENCE*" "${library}/COPYING*" "${library}/license*")
        if(NOT found)
            list(APPEND missing "${library}")
            continue()
        endif()
        list(SORT found)
        list(GET found 0 file)
        file(COPY_FILE "${file}" "${folder}/${name}.txt")
    endforeach()
    if(missing)
        list(JOIN missing "\n  " missing)
        message(FATAL_ERROR "No licence file at the top of:\n  ${missing}\n"
            "Name each one's licence: core_ship_resources(... LICENSES <name>=<file>)")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
        file(GLOB runtime "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/licenses/wasi-sdk/*")
        file(COPY ${runtime} DESTINATION "${folder}")
    endif()
    set(${out} "${folder}" PARENT_SCOPE)
endfunction()
