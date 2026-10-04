#!/bin/bash
# The Windows build's tests, run on Windows itself from WSL (make wintest): the exes cross-built
# in build-win (make windows) and your copy of the game are put in a folder on the Windows side
# (programs started from WSL's own folders get a \\wsl.localhost path, which Windows tools do
# not all take), then run there:
#   - the test tools: ep_flow, ep_badinput, ep_screencheck, ep_edgecheck, ep_savecheck;
#   - the game itself with its files in a folder named "Jürgen tést", drawing offscreen with
#     no sound for a few seconds: it must find them, start and draw (--shots);
#   - build-win/ep_subsys-wsl, for make windifftest: the comparisons against the original
#     with the Windows ep_subsys.exe.
# Prints each result; exits 1 if any failed.
#   tests/wintest.sh [build-win] [Windows folder, default %USERPROFILE%\witchspace-test]
set -u
build=${1:-build-win}
src=$(cd "$(dirname "$0")/.." && pwd)
if [ $# -ge 2 ]; then
    win=$2
else
    profile=$(cmd.exe /c 'echo %USERPROFILE%' 2> /dev/null | tr -d '\r') || profile=
    [ -n "$profile" ] || { echo "wintest: no Windows here (WSL's interop is needed)"; exit 1; }
    win=$(wslpath "$profile")/witchspace-test
fi
mkdir -p "$win/bin" "$win/original" || exit 1
cp "$build"/*.exe "$win/bin/" || exit 1
for f in ELITE.EXE ELITE.GRF ADBLUE.MID; do cp "$src/original/$f" "$win/original/" || exit 1; done
cd "$win" || exit 1 # a Windows folder: no UNC working directory for what is started
export EP_ORIGINAL=$win/original
failed=""

for t in ep_flow ep_badinput ep_screencheck ep_edgecheck; do
    out=$("$src/tests/wintool.sh" "bin/$t.exe" 2>&1 | tr -d '\r')
    status=${PIPESTATUS[0]}
    printf '%-16s %s\n' "$t" "$(echo "$out" | tail -1)"
    [ "$status" = 0 ] || { echo "$out"; failed="$failed $t"; }
done
rm -rf "$win/savecheck.tmpdir"
out=$("$src/tests/wintool.sh" bin/ep_savecheck.exe "$win/savecheck.tmpdir" 2>&1 | tr -d '\r')
status=${PIPESTATUS[0]}
printf '%-16s %s\n' ep_savecheck "$(echo "$out" | tail -1)"
[ "$status" = 0 ] || { echo "$out"; failed="$failed ep_savecheck"; }

# the game, in a folder whose name is not ASCII (the manifest's UTF-8 file names)
game="$win/Jürgen tést"
rm -rf "$game" && mkdir -p "$game/shots" && cp "$win/original/"* "$game/"
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy WSLENV=SDL_VIDEODRIVER:SDL_AUDIODRIVER
"$src/tests/wintool.sh" bin/witchspace.exe --data "$game" --shots "$game/shots" > "$win/game.log" 2>&1 &
pid=$!
sleep 8
taskkill.exe /F /IM witchspace.exe > /dev/null 2>&1
wait $pid 2> /dev/null
shots=$(ls "$game/shots" | wc -l)
if [ "$shots" -gt 0 ]; then
    echo "witchspace       started from \"Jürgen tést\" and drew ($shots pictures)"
else
    echo "witchspace       drew nothing from \"Jürgen tést\":"
    tr -d '\r' < "$win/game.log"
    failed="$failed witchspace"
fi

# for make windifftest: ep_subsys.exe as re/emu/subtest.py calls a tool
printf '#!/bin/sh\nexport EP_ORIGINAL="%s"\nexec "%s/tests/wintool.sh" "%s/bin/ep_subsys.exe" "$@"\n' \
    "$win/original" "$src" "$win" > "$src/$build/ep_subsys-wsl"
chmod +x "$src/$build/ep_subsys-wsl"

if [ -n "$failed" ]; then
    echo "wintest FAILED:$failed"
    exit 1
fi
echo "wintest: all passed on Windows"
