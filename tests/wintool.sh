#!/bin/bash
# A Windows test tool run from WSL, standing in for the Linux one (make wintest, make windifftest):
# arguments that are WSL paths are given to it as Windows paths, and so is $EP_ORIGINAL.
#   tests/wintool.sh TOOL.exe [args...]
exe=$1
shift
args=()
for a in "$@"; do
    case $a in
    /*) args+=("$(wslpath -w "$a")") ;;
    *) args+=("$a") ;;
    esac
done
export WSLENV=EP_ORIGINAL/p
exec "$exe" "${args[@]}"
