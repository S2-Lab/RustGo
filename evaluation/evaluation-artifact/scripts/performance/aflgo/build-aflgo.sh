#!/bin/bash
set -eo pipefail

PROGRAM="$1"
# ==============================
# Step 1: Setting up Environment
# ==============================
export AFLGO="$REPO_ROOT/evaluation/compared-fuzzer/build/aflgo/aflgo"
export PATH="$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/bin:$PATH"
export LIBRARY_DIR="$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"
export HARNESS_DIR="$PWD"

if [[ -z "$PROGRAM" ]]; then
    echo "Usage: $0 <program-name>" >&2
    exit 2
fi

mkdir -p "$HARNESS_DIR/temp/dot-files"

# ==============================
# Step 1: Build LLVM IR from fuzz target
# ==============================
echo "[*] Building fuzz target with LLVM IR emission"
RUSTFLAGS="--emit=llvm-bc" cargo build --target x86_64-unknown-linux-gnu --bin fuzz-target
echo "[*] Merging LLVM IR files"
python3 merge-aflgo.py $PROGRAM

#Target Location Information
export TMP_DIR=$PWD/temp

#Your binary and .bc code must be in the same directory path (SUBJECT)
export SUBJECT=$PWD

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
# Step 2: AFLGO Instrumentation and Binary Generation
# ==============================
# #Preprocess for aflgo distance calculation
opt-11 -load $AFLGO/afl-llvm-pass.so -targets=$TMP_DIR/BBtargets.txt -outdir=$TMP_DIR -O0 total.bc -o output.bc

cat $TMP_DIR/BBnames.txt | rev | cut -d: -f2- | rev | sort | uniq > $TMP_DIR/BBnames2.txt && mv $TMP_DIR/BBnames2.txt $TMP_DIR/BBnames.txt
cat $TMP_DIR/BBcalls.txt | sort | uniq > $TMP_DIR/BBcalls2.txt && mv $TMP_DIR/BBcalls2.txt $TMP_DIR/BBcalls.txt

python3 "$AFLGO/scripts/gen_distance_fast.py" "$SUBJECT" "$TMP_DIR"

opt-11 -load $AFLGO/afl-llvm-pass.so -distance=$TMP_DIR/distance.cfg.txt -O0 output.bc -o output.bc

llc-11 -relocation-model=pic -filetype=obj output.bc -o output.o

clang-11 output.o -o output \
    -L$LIBRARY_DIR -lstd-6f5658153d127ddd \
    -l afl-llvm-rt -L $AFLGO -lpthread -ldl -lm \
    -Wl,--start-group $FRLIBS $OBJECTS $RLIBS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group \
       
rm -f output.bc total.bc output.o
