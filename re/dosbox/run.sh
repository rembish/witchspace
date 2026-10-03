#!/bin/sh
# Run an executable from original/ in DOSBox-X on a private Xvfb display, type keys and
# take screenshots.
#   run.sh [-e EXE] [-k "autotype keys"] [-t seconds] [-s "t1 t2 ..."] [-o outdir]
# Screenshots are written to OUTDIR/shot-<t>.png at the given times (seconds after start).
# While it runs, more keys can be sent with: DISPLAY=:$DISP xdotool key ...
set -e
here=$(cd "$(dirname "$0")" && pwd)
orig=$here/../../original
exe=elite_unpacked.exe keys="p m" secs=20 shots="5 10 15" out=${TMPDIR:-/tmp}/elite-dosbox
DISP=${DISP:-77}
while getopts e:k:t:s:o: o; do case $o in
    e) exe=$OPTARG ;; k) keys=$OPTARG ;; t) secs=$OPTARG ;; s) shots=$OPTARG ;; o) out=$OPTARG ;;
    *) exit 2 ;; esac; done
mkdir -p "$out/c"
cp "$orig"/ELITE.GRF "$orig"/*.MID "$out/c/"
cp "$orig/$exe" "$out/c/GAME.EXE"
cat > "$out/dosbox.conf" <<CONF
[dosbox]
quit warning=false
captures=$out
[cpu]
cycles=20000
[autoexec]
mount c $out/c
c:
autotype -w 2 -p 1 $keys
GAME.EXE
CONF
pgrep -f "Xvfb :$DISP" >/dev/null || { Xvfb ":$DISP" -screen 0 1024x768x24 >/dev/null 2>&1 & sleep 1; }
DISPLAY=:$DISP timeout -s KILL "$secs" dosbox-x -conf "$out/dosbox.conf" >"$out/dosbox.log" 2>&1 &
t0=0
for t in $shots; do
    sleep $((t - t0)); t0=$t
    ffmpeg -loglevel error -y -f x11grab -video_size 1024x768 -i ":$DISP" -frames:v 1 "$out/shot-$t.png"
done
echo "$out"
