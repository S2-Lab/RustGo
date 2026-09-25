#!/bin/bash
# Setting WindRanger Fuzzer Environment
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:detect_leaks=0:symbolize=1}"
export FISHFUZZ="$REPO_ROOT/evaluation/compared-fuzzer/build/fishfuzz/FishFuzz/FF_AFL"
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"
export TMP_DIR=$PWD/temp

exec "$FISHFUZZ/afl-fuzz" -i in -o out -m none -t 1000 -- "$PWD/output"
