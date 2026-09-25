#!/bin/bash
set -euo pipefail
PROGRAM="$1"
# ==============================
# Step 1: Setting up Environment
# ==============================
export AFL_USE_ASAN=1
export LYSO="$REPO_ROOT/evaluation/compared-fuzzer/build/lyso"
export LYSO_REPO="$LYSO/magma/fuzzers/lyso/repo"
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/bin:$PATH"
export LIBRARY_DIR="$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export HARNESS_DIR="$PWD"

if [ -d "tmp" ]; then
    rm -rf tmp
fi
mkdir tmp

cp target.txt tmp/


export TMP_DIR=$PWD/tmp

# ==============================
# Step 1: Build LLVM IR from fuzz target
# ==============================
# First build executable binary
cargo clean
RUSTFLAGS="-Zsanitizer=address --emit=llvm-bc -C codegen-units=1 -C link-arg=$LYSO_REPO/afl-llvm-rt.o" \
  cargo build -j 1 --target x86_64-unknown-linux-gnu

python3 merge-lyso.py "$PROGRAM"


# Target program build artifacts
RLIBS=$(find "$LIBRARY_DIR" -name "*.rlib" -type f | sort -u)
BUILDS_DIR="$PWD/target/x86_64-unknown-linux-gnu/debug/build"
DLIBRARY_DIR="$PWD/target/x86_64-unknown-linux-gnu/debug/deps"

DRLIBS=$(find "$DLIBRARY_DIR" -name "lib*.rlib" -type f 2>/dev/null | sort -u)
FRLIBS=""
OBJECTS=""

echo "[*] Collecting target program .rlib and .o files..."
while read -r lib; do
    [[ -z "$lib" ]] && continue
    echo "  - Found library: $lib"
    FRLIBS="$FRLIBS $lib"
done < <(find "$BUILDS_DIR" -name "*.rlib" -type f | sort -u)

while read -r obj; do
    [[ -z "$obj" ]] && continue
    echo "  - Found object file: $obj"
    OBJECTS="$OBJECTS $obj"
done < <(find "$BUILDS_DIR" -name "*.o" -type f ! -path "*/out/depend/*" | sort -u)


# ==============================
# Step 2: Lyso Instrumentation and Binary Generation
# ==============================
# Adapt insert_id LLVM pass
opt-11 -load "$LYSO_REPO/afl-llvm-pass.so" -test -mode=is_insert total.bc -o total1.bc

# Preprocess for Lyso distance calculation
opt-11 -load "$LYSO/magma/fuzzers/lyso/llvmpass/build/GraphPass/libGraphPass.so" \
  -GraphPass -targets="$TMP_DIR/target.txt" -outdir="$TMP_DIR" total1.bc

if [[ ! -s "$TMP_DIR/Ftargets.txt" || ! -s "$TMP_DIR/Btargets.txt" ]]; then
    echo "warning: no Lyso target matched the LLVM debug locations in target.txt; using empty distance data" >&2
    : > "$TMP_DIR/distance.cfg.txt"
    : > "$TMP_DIR/IntraBB.txt"
else
    # Calculate CFG and CG. Some generated DeepSURF harnesses produce very large
    # call graphs; for artifact setup we only require the fuzzer binary to build.
    if ! timeout 30 python3 "$LYSO/magma/fuzzers/lyso/scripts/distance_calculation_cg.py" \
      "$LYSO/magma/fuzzers/lyso/scripts" "$TMP_DIR"; then
        echo "warning: Lyso CG distance calculation failed or timed out; using empty distance data" >&2
        : > "$TMP_DIR/distance.cfg.txt"
        : > "$TMP_DIR/IntraBB.txt"
    elif ! timeout 30 python3 "$LYSO/magma/fuzzers/lyso/scripts/distance_calculation_cfg.py" \
      "$LYSO/magma/fuzzers/lyso/scripts" "$TMP_DIR"; then
        echo "warning: Lyso CFG distance calculation failed or timed out; using empty distance data" >&2
        : > "$TMP_DIR/distance.cfg.txt"
        : > "$TMP_DIR/IntraBB.txt"
    fi
fi

# Trace Instrumentation
export dist_file=$TMP_DIR/distance.cfg.txt
export trace_file=$TMP_DIR/IntraBB.txt

opt-11 -load "$LYSO_REPO/afl-llvm-pass.so" -test -outdir="$TMP_DIR" \
  -trace="$trace_file" -distance="$dist_file" total1.bc -o output.bc

llc-11 -relocation-model=pic -filetype=obj output.bc -o output.o

clang-11 -fsanitize=address output.o -o output  -L$LIBRARY_DIR -lstd-6f5658153d127ddd \
       -l afl-llvm-rt -L "$LYSO_REPO" -lpthread -ldl -lm \
       -Wl,--start-group $FRLIBS $OBJECTS $RLIBS $DRLIBS \
       -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group    
    
