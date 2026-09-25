#!/bin/bash
export RUSTGoPrun="$REPO_ROOT/evaluation/ablation-variants/prun/RustGoPrun/afl.rs/AFLplusplus"

$RUSTGoPrun/afl-fuzz -i in -o "$PWD/out" -- "$PWD/rustgo-prun" 

