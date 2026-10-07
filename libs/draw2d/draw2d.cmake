# Our own drawing for a gpu::View (libs/gpu): shapes, lines and text with
# WebGPU (draw2d.h), flexbox-style layout with Clay (layout.h), and knobs,
# sliders and drag areas bound to the plugin's parameters (ui.h).
#
#   include(libs/draw2d/draw2d.cmake)
#   draw2d_add_to(<target>)      the impl target natively, the page target in WCLAP
include_guard(DIRECTORY)
include(FetchContent)

set(DRAW2D_DIR "${CMAKE_CURRENT_LIST_DIR}")
# stb_truetype rasterizes the font; Clay lays out boxes. Both are single headers.
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
    URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
    SOURCE_SUBDIR none) # headers only: don't run its CMakeLists
FetchContent_Declare(clay
    URL https://github.com/nicbarker/clay/archive/refs/tags/v0.14.tar.gz
    URL_HASH SHA256=ee8f6477020dd72afe8cf6f8d3ab6855980028289a2b677a1423d17aab983585
    SOURCE_SUBDIR none) # its CMakeLists builds examples we don't want
FetchContent_MakeAvailable(stb clay)

function(draw2d_add_to target)
    target_sources(${target} PRIVATE "${DRAW2D_DIR}/draw2d.cpp" "${DRAW2D_DIR}/layout.cpp" "${DRAW2D_DIR}/ui.cpp")
    target_include_directories(${target} PRIVATE "${stb_SOURCE_DIR}" "${clay_SOURCE_DIR}")
endfunction()
