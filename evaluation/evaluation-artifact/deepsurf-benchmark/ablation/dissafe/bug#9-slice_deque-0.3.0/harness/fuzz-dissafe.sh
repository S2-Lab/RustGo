#!/bin/bash
export AFLRS="$REPO_ROOT/evaluation/ablation-variants/dissafe/RustGoDissafe/afl.rs/AFLplusplus"

$AFLRS/afl-fuzz -i in -o out $PWD/rustgo-dissafe 
