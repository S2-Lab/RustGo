

#!/bin/bash
# Setting AFLGO Fuzzer Environment
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:detect_leaks=0:symbolize=0:use_sigaltstack=0}"
export AFL_USE_ASAN=1
export AFLGO="$REPO_ROOT/evaluation/compared-fuzzer/build/aflgo/aflgo"
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"
export AFL_SKIP_CPUFREQ=1
export TMP_DIR=$PWD/temp

#Checking Artifact Root
if [ -z "$ARTIFACT_ROOT" ]; then
    echo "[+] ARTIFACT_ROOT is not set."
    echo "[+] Please run 'source env.sh' in the top directory of this artifact."
    exit 1
fi

# Run AFLGO fuzzing
export AFL_USE_ASAN=1
"$AFLGO/afl-fuzz" -z exp -c 45m -m none -i in -o out -- "$PWD/output"
