#!/bin/sh
# Put the licences that go with the web version into a folder (the web zip, the Pages site):
# ours, Nuked OPL3's, and those of what Emscripten links in (its runtime, musl libc, its SDL2
# port). Run from the source tree with Emscripten's environment set (emcc on the PATH), after
# the web build, which fetched the SDL2 port into Emscripten's cache.
#   web/licenses.sh <folder>
set -eu
dest=$1
em=$(dirname "$(readlink -f "$(command -v emcc)")")
sdl=$(ls -d "$(em-config CACHE)"/ports/sdl2/SDL-*/LICENSE.txt | tail -1)

cp LICENSE "$dest/LICENSE.txt"
cp THIRD_PARTY.md "$dest/"
cp third_party/nuked-opl3/LICENSE "$dest/LICENSE.Nuked-OPL3.txt"
cp "$em/LICENSE" "$dest/LICENSE.Emscripten.txt"
cp "$em/system/lib/libc/musl/COPYRIGHT" "$dest/LICENSE.musl.txt"
cp "$sdl" "$dest/LICENSE.SDL2.txt"
