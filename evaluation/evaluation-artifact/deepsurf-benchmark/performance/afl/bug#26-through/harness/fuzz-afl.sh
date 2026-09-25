#!/bin/bash
# Setting AFL Fuzzer Environment
export AFL="$REPO_ROOT/evaluation/compared-fuzzer/build/afl/AFL"
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"

#Run AFL
"$AFL/afl-fuzz" -m none -i in -o out -- "$PWD/output"