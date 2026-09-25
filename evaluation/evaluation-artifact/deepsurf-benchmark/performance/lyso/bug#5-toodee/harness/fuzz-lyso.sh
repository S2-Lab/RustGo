#!/bin/bash
# Setting Lyso Fuzzer Environment
export AFL_SKIP_CPUFREQ=1
export AFL_NO_AFFINITY=1
export AFL_USE_ASAN=1
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}abort_on_error=1:detect_leaks=0:symbolize=0:use_sigaltstack=0"
export TMP_DIR=$PWD/tmp
export SHARED=$PWD/shared
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p "$SHARED/findings"
export LYSO="$REPO_ROOT/evaluation/compared-fuzzer/build/lyso"

exec "$LYSO/magma/fuzzers/lyso/repo/afl-fuzz" \
  -d -g "$TMP_DIR" -c "$SHARED" -m none \
  -i in -o "$SHARED/findings" -- "$PWD/output" 2>&1
