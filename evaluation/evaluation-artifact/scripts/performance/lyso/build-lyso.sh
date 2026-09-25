#!/bin/bash
set -euo pipefail
PROGRAM="$1"
# ==============================
# Step 1: Setting up Environment
# ==============================
export AFL_USE_ASAN=1
export LYSO="$REPO_ROOT/evaluation/compared-fuzzer/build/lyso"
export LYSO_LD="$REPO_ROOT/evaluation/compared-fuzzer/build/lyso/magma/fuzzers/lyso/repo"
export LYSO_LLVM="$LYSO/magma/fuzzers/lyso/llvm-11/bin"
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:${LD_LIBRARY_PATH:-}"
export PATH="$LYSO_LLVM:$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/bin:$PATH"
export LIBRARY_DIR="$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export HARNESS_DIR="$PWD"

OPT="$LYSO_LLVM/opt"
LLC="$LYSO_LLVM/llc"
CLANG="$LYSO_LLVM/clang"

for tool in "$OPT" "$LLC" "$CLANG"; do
    if [[ ! -x "$tool" ]]; then
        echo "Missing Lyso LLVM tool: $tool" >&2
        exit 1
    fi
done
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
RUSTFLAGS="--emit=llvm-bc" cargo build --target x86_64-unknown-linux-gnu --bin fuzz-target

python3 merge-lyso.py $PROGRAM


# Target program build artifacts
RLIBS=$(find "$LIBRARY_DIR" -name "*.rlib" -type f | sort -u)
BUILDS_DIR="$HARNESS_DIR/target/x86_64-unknown-linux-gnu/debug/build"
DLIBRARY_DIR="$HARNESS_DIR/target/x86_64-unknown-linux-gnu/debug/deps"

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
"$OPT" -load "$LYSO/magma/fuzzers/lyso/repo/afl-llvm-pass.so" -test -mode=is_insert total.bc -o total1.bc

# Preprocess for Lyso distance calculation
"$OPT" -load "$LYSO/magma/fuzzers/lyso/llvmpass/build/GraphPass/libGraphPass.so" -GraphPass -targets="$TMP_DIR/target.txt" -outdir="$TMP_DIR" total1.bc

# Calculate CFG and CG
chmod +x "$LYSO/magma/fuzzers/lyso/scripts"/distance*.py
python3 "$LYSO/magma/fuzzers/lyso/scripts/distance_calculation_cg.py" "$LYSO/magma/fuzzers/lyso/scripts" "$TMP_DIR"
python3 "$LYSO/magma/fuzzers/lyso/scripts/distance_calculation_cfg.py" "$LYSO/magma/fuzzers/lyso/scripts" "$TMP_DIR"

# Trace Instrumentation
export dist_file=$TMP_DIR/distance.cfg.txt
export trace_file=$TMP_DIR/IntraBB.txt

"$OPT" -load "$LYSO/magma/fuzzers/lyso/repo/afl-llvm-pass.so" -test -outdir="$TMP_DIR" -trace="$trace_file" -distance="$dist_file" total1.bc -o output.bc

"$LLC" -relocation-model=pic -filetype=obj output.bc -o output.o

"$CLANG" output.o -o output  -L$LIBRARY_DIR -lstd-6f5658153d127ddd \
       -lpthread -ldl -lm -L$LYSO_LD -lafl-llvm-rt \
       -Wl,--start-group $FRLIBS $OBJECTS $RLIBS $DRLIBS \
       -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group    

rm -f output.* total*
