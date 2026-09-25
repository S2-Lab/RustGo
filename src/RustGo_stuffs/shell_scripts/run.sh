#!/bin/bash
# start=$(date +%s%6N)
set -e

LIBRARY_DIR="$HOME/s2fuzz/scripts/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
RLIBS=$(find $LIBRARY_DIR -name "*.rlib")

COMPILER_RT_DIR="$HOME/.local/share/afl.rs/rustc-1.64.0-dev/afl.rs-0.12.9/afl-llvm-rt"
CLOUDFLARE="$HOME/fuzz-test/awesome-rust/image-processing/oxipng/target/debug/build/cloudflare-zlib-sys-9120dc9c6845dcf1/out"
DEFLATE="$HOME/fuzz-test/awesome-rust/image-processing/oxipng/target/debug/build/libdeflate-sys-3c535a8d6f4b2bf4/out/lib"


#--release --target x86_64-unknown-linux-gnu

# (1) Build the program and Preprocess it
RUSTFLAGS="--emit=llvm-ir --print link-args" cargo build > ERASAN_linking_commands.txt
cp preprocess.sh ERASAN_linking_commands.txt target/debug/deps/ && cd target/debug/deps/
FILES=$(./preprocess.sh)
echo $FILES
llvm-link -S $FILES -o total.ll
mv total.ll ../../../ && cd ../../../
# echo "Build the program and Preprocess DONE!"

# handling linking error for fdeflate
DLIBRARY_DIR="$HOME/fuzz-test/awesome-rust/image-processing/oxipng/target/debug/deps"
# DRLIBS=$(find $DLIBRARY_DIR -name "libfdeflate-ef2821f2912e6868.rlib" "libclap-97467da07f257d87.rlib")
DRLIBS=$(find $DLIBRARY_DIR \( -name "libfdeflate-ef2821f2912e6868.rlib" -o -name "libclap-97467da07f257d87.rlib" -o -name "liboxipng-bca7c3d1a62aadf7.rlib" \))
echo $DRLIBS
# # (2) Conduct the analysis
# s2pidy total.ll -afl
# echo "Static Analysis DONE!"

# (3) Compile the program with SanitizerCoverage
opt -passes=sancov-module -sanitizer-coverage-level=3 -sanitizer-coverage-trace-pc-guard -sanitizer-coverage-prune-blocks=0 -S total.ll -o sancov.ll

# (4) Compile the program with ASAN
opt --asan-module -S sancov.ll -o s2fuzz.ll
llvm-as s2fuzz.ll -o s2fuzz.bc
llc -relocation-model=pic -filetype=obj s2fuzz.bc -o s2fuzz.o
clang s2fuzz.o -o s2fuzz -L$CLOUDFLARE -lcloudflare_zlib -L$DEFLATE -L$DLIBRARY_DIR -l deflate -L$LIBRARY_DIR -L$COMPILER_RT_DIR -l afl-llvm-rt -lstd-bc021c2817d56996 -lpthread -ldl -lm -fsanitize=address $RLIBS $DRLIBS $RLIBS $DRLIBS

# end=$(date +%s%6N)
# runtime=$((end-start))
# echo "Done!!"
# echo "Compile Time : $runtime ns"