#!/bin/bash
# Setting AFLRUN Fuzzer Environment
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/1.69.0-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"
export PATH="$HOME/.rustup/toolchains/1.69.0-x86_64-unknown-linux-gnu/bin:$PATH"
export AFLRUN="$REPO_ROOT/evaluation/compared-fuzzer/build/AFLRun/AFLRun"
export AFLRUN_BB_TARGETS="$PWD/temp/BBtargets.txt"
export AFLRUN_TARGETS="::"
export AFLRUN_TEMP_DIR="$PWD/temp"

exec "$AFLRUN/afl-fuzz" -i in -o out -- "$PWD/output"
