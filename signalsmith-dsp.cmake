# Signalsmith Audio's DSP library (MIT, header-only), for its FFT: the spectrum
# analysis (chardsp_Spectrum.h) includes "dsp/fft.h".
include_guard(DIRECTORY)
include(FetchContent)
FetchContent_Declare(signalsmith_dsp
    URL https://github.com/Signalsmith-Audio/dsp/archive/4f62b0a8783c483c353d0232654fe0ffea3cd434.tar.gz
    URL_HASH SHA256=78f5c5a2404d89e9b951d1c0c322f3b22600e21c1bccf188bd93eb41a4b2546a
    SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/signalsmith/dsp" # so includes read "dsp/fft.h"
    SOURCE_SUBDIR none) # headers only: don't run its CMakeLists
FetchContent_MakeAvailable(signalsmith_dsp)
set(SIGNALSMITH_DSP_INCLUDE "${FETCHCONTENT_BASE_DIR}/signalsmith")
set_property(GLOBAL APPEND PROPERTY CORE_THIRD_PARTY_LICENSES
    "signalsmith=${FETCHCONTENT_BASE_DIR}/signalsmith/dsp/LICENSE.txt")
