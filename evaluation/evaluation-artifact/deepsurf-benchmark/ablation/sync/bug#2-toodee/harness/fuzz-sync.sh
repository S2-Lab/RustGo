#!/bin/bash
export RUSTGo_Sync="$REPO_ROOT/evaluation/ablation-variants/sync/RustGoSync/afl.rs/AFLplusplus"

$RUSTGo_Sync/afl-fuzz -i in -o "$PWD/out" "$PWD/rustgo-sync" 