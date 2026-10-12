# Third-party software

This plugin is built with, and its binaries include, the following. CMake downloads them: char-clap-template at the version pinned in CMakeLists.txt, which in turn pins the others in its libs/*/*.cmake, and Signalsmith's DSP library in signalsmith-dsp.cmake. Keep their notices with anything you distribute. Every build ships their full licence texts in THIRD_PARTY_LICENSES/.

- char-clap-template, whose libraries (libs/) this is built on: MIT. https://github.com/charCulbert/char-clap-template
- CLAP SDK: MIT. https://github.com/free-audio/clap
- clap-wrapper, and the VST3 SDK it downloads: MIT (see the SDK's own licence). https://github.com/free-audio/clap-wrapper
- AudioUnit SDK (clap-wrapper downloads it for the AU, on macOS): Apache-2.0. https://github.com/apple/AudioUnitSDK
- RtAudio (clap-wrapper downloads it for the standalone app's audio): MIT-style (RtAudio licence). https://github.com/thestk/rtaudio
- RtMidi (clap-wrapper downloads it for the standalone app's MIDI): MIT-style (RtMidi licence). https://github.com/thestk/rtmidi
- ghc::filesystem (clap-wrapper downloads it on macOS, unless CMAKE_OSX_DEPLOYMENT_TARGET is 10.15 or later): MIT. https://github.com/gulrak/filesystem
- Dawn: BSD 3-Clause. https://dawn.googlesource.com/dawn
- AccessKit (its prebuilt libraries; also contain Rust crates under their own licences): MIT or Apache-2.0, with a BSD 3-Clause notice from Chromium. https://github.com/AccessKit/accesskit
- stb_truetype, stb_image and stb_textedit: MIT or public domain. https://github.com/nothings/stb
- Clay: zlib. https://github.com/nicbarker/clay
- nanosvg: zlib. https://github.com/memononen/nanosvg
- Signalsmith Audio DSP (its FFT): MIT. https://github.com/Signalsmith-Audio/dsp
- Signalsmith Basics (AnalyzerSpectrum.h follows its analyser; licence in licenses/SignalsmithBasics.txt): MIT. https://github.com/Signalsmith-Audio/basics
- Cividis (a colour scheme in chardraw's views.h; licence in the template's libs/chardraw/licenses/cividis.txt): BSD-style (Battelle Memorial Institute). https://github.com/pnnl/cmaputil
- Turbo (a colour scheme in chardraw's views.h; licence in the template's libs/chardraw/licenses/turbo.txt): Apache-2.0. https://gist.github.com/mikhailov-work/6a308c20e494d9e0ccc29036b28faa7a
- IBM Plex Mono (font, in resources/fonts with its licence): SIL Open Font License 1.1. https://github.com/IBM/plex
- Emscripten's runtime and emdawnwebgpu, in the WCLAP's web page only: MIT or NCSA (Emscripten), BSD 3-Clause (emdawnwebgpu), plus musl and libc++. https://emscripten.org
