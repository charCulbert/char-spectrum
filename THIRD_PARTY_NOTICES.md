# Third-party software

This plugin is built with, and its binaries include, the following. CMake downloads them (pinned in libs/*/*.cmake and signalsmith-dsp.cmake); keep their notices with anything you distribute.

- CLAP SDK: MIT. https://github.com/free-audio/clap
- clap-wrapper, and the VST3 and AudioUnit SDKs it downloads: MIT (see each SDK's own licence). https://github.com/free-audio/clap-wrapper
- Dawn, the WebGPU implementation (natively), and its Emscripten bindings (in the WCLAP): BSD 3-Clause. https://dawn.googlesource.com/dawn
- Clay, for layout: zlib. https://github.com/nicbarker/clay
- stb_truetype, for text: MIT or public domain. https://github.com/nothings/stb
- Signalsmith Audio's DSP library, for its FFT: MIT. https://github.com/Signalsmith-Audio/dsp
- IBM Plex Mono (`resources/fonts/`, with its licence there): SIL Open Font License 1.1. https://github.com/IBM/plex
