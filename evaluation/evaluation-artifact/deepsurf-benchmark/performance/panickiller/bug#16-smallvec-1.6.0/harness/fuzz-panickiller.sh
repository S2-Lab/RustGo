#!/bin/bash
set -Eeuo pipefail
export PANICKILLER_DIR="$REPO_ROOT/evaluation/compared-fuzzer/build/panickiller"
export PK_LD="$PANICKILLER_DIR/"
export PK="$PANICKILLER_DIR/afl.rs/AFLplusplus"


exec "$PK/afl-fuzz" -i in -o "$PWD/out" -- "$PWD/pk-target"
