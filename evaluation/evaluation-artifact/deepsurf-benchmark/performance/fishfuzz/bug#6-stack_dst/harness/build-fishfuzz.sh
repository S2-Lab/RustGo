#!/bin/bash
set -Eeuo pipefail
PROGRAM="$1"
# ==============================
# Step 1: Setting up Environment
# ==============================
export FISHFUZZ="$REPO_ROOT/evaluation/compared-fuzzer/build/fishfuzz/FishFuzz/FF_AFL"
export PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"
export LIBRARY_DIR="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export HARNESS_DIR="$PWD"
rm -rf temp
mkdir -p temp/dot-files
export TMP_DIR=$PWD/temp

# ==============================
# Step 1: Build LLVM IR from fuzz target
# ==============================
RUSTFLAGS="-Zsanitizer=address --emit=llvm-ir -C codegen-units=1 -C link-arg=$FISHFUZZ/afl-llvm-rt.o" \
  cargo build --target x86_64-unknown-linux-gnu

python3 merge-fishfuzz.py $PROGRAM


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
# LLVM Pass and Generate Object Code
# ==============================
# (1) Coverage Instrumentation
# Input : total.ll / Output: total_final.ll
opt-12 -load "$FISHFUZZ/afl-llvm-pass.so" -test -outdir="$TMP_DIR" -pmode=conly total.ll -o total_final.ll

# (2) Analysis Mode
# Input : total_target.ll / Output: total_temp.ll && some files in TMP_DIR
opt-12 -load "$FISHFUZZ/afl-llvm-pass.so" -test -outdir="$TMP_DIR" -pmode=aonly total_final.ll -o total_temp.ll

# (3) Callgraph Creation
# Input : total.ll / Output : total.ll.callgraph.dot
opt-12 -dot-callgraph total.ll && mv total.ll.callgraph.dot $TMP_DIR/dot-files/callgraph.dot

# (4) Static distance map calculation
"$FISHFUZZ/scripts/gen_initial_distance.py" "$TMP_DIR"

# (5) Generating final target
# Input : total_final.ll / Output : output.ll

opt-12 -load "$FISHFUZZ/afl-llvm-pass.so" -test -pmode=fonly -funcid="$TMP_DIR/funcid.csv" -outdir="$TMP_DIR" total_final.ll -o output.ll

# (6) Generating Object file
llc-12 -relocation-model=pic -filetype=obj output.ll -o output.o

# (7) Generating Binary
clang++-12 output.o "$FISHFUZZ/afl-compiler-rt.o" -o output -L"$FISHFUZZ" -lAFL -ldl -lpthread -lrt -lm \
    -Wl,-rpath,"$LIBRARY_DIR" -Wl,-rpath,"$FISHFUZZ" \
    -L"$LIBRARY_DIR" -lstd-1a95cddb5b8aaf6b -fsanitize=address \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group 

# (8) Clean up
rm -f output.ll output.o total*
