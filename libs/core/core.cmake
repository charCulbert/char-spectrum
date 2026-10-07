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
# it (at the WCLAP's root). The copy runs on every build, so edits to the
# folder need no relink. Pass DEPENDS <target> when a build step assembles the
# folder.
function(core_ship_resources name folder)
    cmake_parse_arguments(ARG "" "" "DEPENDS" ${ARGN})
    set(notices "")
    foreach(file LICENSE THIRD_PARTY_NOTICES.md)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
            list(APPEND notices "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
        endif()
    endforeach()
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
        if(folder OR notices)
            set(copyFolder "")
            if(folder)
                set(copyFolder COMMAND ${CMAKE_COMMAND} -E copy_directory "${folder}" "${destination}")
            endif()
            set(copyNotices "")
            if(notices)
                set(copyNotices COMMAND ${CMAKE_COMMAND} -E copy ${notices} "${destination}")
            endif()
            # A copy without a relink changes a signed bundle, so sign it again
            # when it already has its binary. (The standalone app's build adds
            # files after this, so it keeps only its signing after linking.)
            set(resign "")
            if(APPLE AND NOT format MATCHES "auv3|aax|standalone")
                set(resign COMMAND /bin/sh -c "[ -z \"$(ls -A \"$1\" 2>/dev/null)\" ] || codesign --force --sign - \"$1/../..\"" sh "$<TARGET_FILE_DIR:${target}>")
            endif()
            add_custom_target(${target}_resources
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${destination}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
                ${copyFolder}
                ${copyNotices}
                ${resign}
                VERBATIM)
            if(ARG_DEPENDS)
                add_dependencies(${target}_resources ${ARG_DEPENDS})
            endif()
            add_dependencies(${target} ${target}_resources)
        endif()
        # The linker signs only the binary; sign the bundle so its resources are
        # sealed too, or `codesign -v` (and stricter hosts) reject it. AUv3 and
        # AAX are signed by their own build steps.
        if(APPLE AND NOT format MATCHES "auv3|aax")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>"
                VERBATIM)
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
