#!/bin/bash
# Setting WindRanger Fuzzer Environment
export WINDRANGER="$REPO_ROOT/evaluation/compared-fuzzer/build/windranger/WindRanger"
export PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"
export WR_BB_TARGETS="$PWD/temp/BBtargets.txt"

"$WINDRANGER/afl-fuzz" -m none -i in -o out -- "$PWD/output"



