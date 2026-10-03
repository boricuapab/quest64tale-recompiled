# Building v1.0.5

Clone with recursive submodules:

```sh
git clone --recursive https://github.com/boricuapab/quest64tale-recompiled.git
cd quest64tale-recompiled
git submodule update --init --recursive
python -m pip install Pillow
python tools/build.py --rom /path/to/your/quest64.z64
```

Use your own ROM path on either platform. The script verifies and normalizes the original USA ROM, fetches hash-checked open fonts, generates original application icons, builds the pinned recompiler, generates the game/RSP code and destination tables, then builds the native application. Generated files and the ROM are ignored by Git.

Windows: run inside an x64 Visual Studio 2022 developer terminal with LLVM 18, CMake, Ninja and GNU make on PATH. The build uses clang-cl and an explicit compatibility opt-out for newer MSVC headers. An up-to-date LLVM toolchain is also suitable.

Linux: install Clang 18, CMake, Ninja, make, SDL2/GTK3/X11 development files, zlib and Vulkan development files. Set `CC=clang-18 CXX=clang++-18` when invoking the build if those are not your defaults. On Ubuntu, the development packages include `libsdl2-dev libgtk-3-dev libx11-dev zlib1g-dev libvulkan-dev`.

The application is written to `build/game`. Put the prepared `assets` folder and controller mapping file beside the binary. Windows additionally needs the SDL2 and DirectX compiler DLLs. Release packages include open UI fonts and the approved launcher image, but never include ROMs, extracted gameplay media or saves.

The build script remaps checkout paths in compiler diagnostics/embedded source names. Audit release binaries and archive contents before publication. Optional extraction scripts require NumPy and a locally built original animation-sampler library; their output must remain private.
