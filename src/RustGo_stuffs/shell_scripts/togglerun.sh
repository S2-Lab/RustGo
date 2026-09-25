#!/bin/bash

LIBRARY_DIR="$HOME/s2fuzz/scripts/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
RLIBS=$(find $LIBRARY_DIR -name "*.rlib")
COMPILER_RT_DIR="$HOME/.local/share/afl.rs/rustc-1.64.0-dev/afl.rs-0.12.9/afl-llvm-rt"
# COMPILER_RT_DIR="$HOME/.local/share/afl.rs/rustc-1.64.0-dev/afl.rs-0.12.1/afl-llvm-rt"
# export AFL_TOGGLE_FUZZ=$HOME/AFL-STUFFS/AFL-ENGINES/toggle_afl_engine

s2pidy test2-2b14f53e4509c0b5.ll -afl
opt -passes=sancov-module -sanitizer-coverage-level=3 -sanitizer-coverage-trace-pc-guard -sanitizer-coverage-prune-blocks=0 -S S2FuzzAnalysis.ll -o sancov.ll
opt --asan-module -S sancov.ll -o asan.ll
llvm-as asan.ll -o output.bc
llc -filetype=obj output.bc -o output.o
clang output.o -o toggle-output -L$LIBRARY_DIR  -lstd-bc021c2817d56996 -lpthread -ldl -lm -fsanitize=address -L$COMPILER_RT_DIR -l afl-llvm-rt $RLIBS $RLIBS

rm sancov.ll asan.ll output.bc output.o
# rm sancov.ll output.bc output.o


# # First get sorted file of ToggleInfo.txt
# # Input : ToggleInfo.txt
# # Output : ToggleInfoSorted.txt
# python3 read_and_sort_file.py

# # Second get condensed file of ToggleInfoSorted.txt
# # Input : ToggleInfoSorted.txt
# # Output : ToggleInfoCondensed.txt
# python3 condense_lines_renumbered.py ToggleInfoSorted.txt ToggleInfoCondensed.txt

# # Third get final file of ToggleInfoNew.txt
# # Input : ToggleInfoCondensed.txt
# # Output : ToggleInfoNew.txt
# python3 remove_subset.py


#AFL_TOGGLE_FUZZ/afl-fuzz -i in -o outdirectory_toggle pwd/toggle-output