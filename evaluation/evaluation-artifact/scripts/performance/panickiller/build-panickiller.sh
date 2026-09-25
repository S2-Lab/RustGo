#!/bin/bash
PROGRAM="$1"
# ==============================
# Step 1: Setting up Environment
# ==============================
export PANICKILLER_DIR="$REPO_ROOT/evaluation/compared-fuzzer/build/panickiller"
export PANICKILLER_COMPILER_RT="$PANICKILLER_DIR/RuntimeLib_PanicKiller/afl-llvm-rt"
export LIBRARY_DIR="$BUILD_DIR/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export PANICKILLER_SCRIPT="$ARTIFACT_ROOT/scripts/performance/panickiller"
export HARNESS_DIR="$PWD"

cd $PANICKILLER_DIR/ERASan
cd env
source env.sh
cd $HARNESS_DIR

# ==============================
# Step 1: Build LLVM IR from fuzz target
# ==============================
echo "[*] Building fuzz-target with LLVM IR emission..."
RUSTFLAGS="--emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu --bin fuzz-target
echo "[*] Merging LLVM IR files..."
python3 merge-panickiller.py $PROGRAM

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
# Step 2: Perform PanicKiller analysis and instrumentation
# ==============================
erasan total.ll -erasan

erasan erasanOptAnalysis.ll -panickiller-inst

echo "[*] Applying LLVM optimization and instrumentation..."
opt -O2 panicKiller.ll -o _panicKiller.ll
opt -passes=sancov-module -sanitizer-coverage-level=3 \
    -sanitizer-coverage-trace-pc-guard -sanitizer-coverage-prune-blocks=0 \
    -S _panicKiller.ll -o __panicKiller.ll

# Now Compile panickiller IR to object
echo "[*] Compiling panicKiller IR to object..."
llvm-as __panicKiller.ll -o pkoutput.bc
llc -filetype=obj pkoutput.bc -o pkoutput.o

echo "[*] Linking final binary..."
clang pkoutput.o -o pk-target -L$LIBRARY_DIR -lstd-bc021c2817d56996 \
    -lpthread -ldl -lm -L$PANICKILLER_COMPILER_RT -lafl-llvm-rt \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
   -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group \

echo "[✔] Build complete. Output: pk-target"


# ==============================
# Step 3: Prepare output directories
# ==============================
echo "[*] Preparing output directories..."

# Clean and recreate panickiller-build & fuzz
if [ -d "panickiller-build" ]; then
    rm -rf panickiller-build
fi
mkdir panickiller-build

if [ -d "panickiller-fuzz" ]; then
    rm -rf panickiller-fuzz
fi
mkdir panickiller-fuzz

echo "[*] Moving and organizing build artifacts..."

# Move build artifacts to panickiller-build
mv *.ll panickiller-build/ || true
mv *.o  panickiller-build/ || true
mv *.bc panickiller-build/ || true
mv *.txt panickiller-build/ || true

# Move compiled binary to panickiller-fuzz
mv pk-target panickiller-fuzz/

# Copy evaluation script and input corpus
cp $PANICKILLER_SCRIPT/fuzz-panickiller.sh panickiller-fuzz/
cp -r $PANICKILLER_SCRIPT/in panickiller-fuzz/

echo "[✔] panickiller instrumentation and setup complete."
