#!/bin/bash

# immediately stop building if error occurs
set -e

# install prerequisite package
echo "build rust compiler runtime library"
export WORKING_DIR_RUNTIME=`pwd`/../src
export RUST=$PWD/rust
pushd $RUST
echo "Seting for Compiler-rt in Rust Compiler"
if [ -f src/bootstrap/native.rs ]; then
    echo "[Removing old native.rs file]"
    rm src/bootstrap/native.rs
fi

if [ -f build/x86_64-unknown-linux-gnu/llvm/llvm-finished-building ]; then
    echo "[Removing old llvm-finished-building file]"
    rm build/x86_64-unknown-linux-gnu/llvm/llvm-finished-building
fi
popd
cp $WORKING_DIR_RUNTIME/rustc-1.64.0/src/bootstrap/native.rs $RUST/src/bootstrap

# Set the ASAN environment
echo "[Setting ASAN environment]"
export WORKING_DIR_ASAN=`pwd`/../src/rustc-1.64.0/src/llvm-project/llvm/lib/Transforms/Instrumentation
export LLVM_DIR_INST=$RUST/src/llvm-project/llvm/lib/Transforms/Instrumentation

if [ -f $LLVM_DIR_INST/AddressSanitizer.cpp ]; then
    echo "[Removing old AddressSanitizer.cpp file]"
    rm $LLVM_DIR_INST/AddressSanitizer.cpp
fi
cp $WORKING_DIR_ASAN/AddressSanitizer.cpp $LLVM_DIR_INST
echo "[Finished Changing Setting ASAN environment]"

# Set the Opt files
echo "[Setting Rust LLVM & Opt files]"
export WORKING_DIR_OPT=`pwd`/../src
# export RUSTC=`pwd`/../src/rustc-1.64.0/compiler

export OPT_FILES=$WORKING_DIR_OPT/rustc-1.64.0/opt-files
pushd $RUST
if [ -f src/llvm-project/llvm/include/llvm/Analysis/CallPrinter.h ]; then
    echo "[Removing old CallPrinter.h file]"
    rm src/llvm-project/llvm/include/llvm/Analysis/CallPrinter.h
fi
cp $OPT_FILES/CallPrinter.h $RUST/src/llvm-project/llvm/include/llvm/Analysis/
popd

pushd $RUST
if [ -f src/llvm-project/llvm/lib/Analysis/CallPrinter.cpp ]; then
    echo "[Removing old CallPrinter.cpp file]"
    rm src/llvm-project/llvm/lib/Analysis/CallPrinter.cpp
fi
cp $OPT_FILES/CallPrinter.cpp $RUST/src/llvm-project/llvm/lib/Analysis/
popd

pushd $RUST
if [ -f src/llvm-project/llvm/lib/Passes/PassBuilder.cpp ]; then
    echo "[Removing old PassBuilder.cpp file]"
    rm src/llvm-project/llvm/lib/Passes/PassBuilder.cpp
fi
cp $OPT_FILES/PassBuilder.cpp $RUST/src/llvm-project/llvm/lib/Passes

if [ -f src/llvm-project/llvm/lib/Passes/PassRegistry.def ]; then
    echo "[Removing old PassRegistry.def file]"
    rm src/llvm-project/llvm/lib/Passes/PassRegistry.def
fi
cp $OPT_FILES/PassRegistry.def $RUST/src/llvm-project/llvm/lib/Passes
popd

pushd $RUST
if [ -f src/llvm-project/llvm/test/Other/heat-colors-graphs.ll ]; then
    echo "[Removing old heat-colors-graphs.ll file]"
    rm src/llvm-project/llvm/test/Other/heat-colors-graphs.ll
fi
cp $OPT_FILES/heat-colors-graphs.ll $RUST/src/llvm-project/llvm/test/Other/
cp $OPT_FILES/heat-colors-multigraph.ll $RUST/src/llvm-project/llvm/test/Other/
popd
echo "[Finished Rust LLVM & Opt files]"

# Set the SanitizerCoverage LLVM Pass
echo "[Setting SanitizerCoverage.cpp with new implementation]"
export WORKING_DIR_SANCOV=`pwd`/../src/RustGo_stuffs/afl-llvm-pass_rust
if [ -f $LLVM_DIR_INST/SanitizerCoverage.cpp ]; then
    echo "[Removing old SanitizerCoverage.cpp file]"
    rm $LLVM_DIR_INST/SanitizerCoverage.cpp
fi
cp $WORKING_DIR_SANCOV/SanitizerCoverage.cpp $LLVM_DIR_INST
echo "[Finished Changing Setting SanitizerCoverage.cpp]"

# LLVM Pass for no safe code coverage (sancov-module-no-safe)
export WORKING_DIR_NEW_PASS=`pwd`/../src/rustc-1.64.0/src/llvm-project/llvm/lib
export WORKING_INCLUDE_NEW_PASS=`pwd`/../src/rustc-1.64.0/src/llvm-project/include/llvm
export LLVM_DIR_NEW_PASS=$RUST/src/llvm-project/llvm/lib
export LLVM_INCLUDE_NEW_PASS=$RUST/src/llvm-project/llvm/include/llvm

# (1) src/llvm-project/llvm/lib/Transforms/Instrumentation/SanitizerCoverageNoSafeCode.cpp
cp $WORKING_DIR_NEW_PASS/Transforms/Instrumentation/SanitizerCoverageNoSafeCode.cpp $LLVM_DIR_NEW_PASS/Transforms/Instrumentation/

# (2) src/llvm-project/llvm/include/llvm/Transforms/Instrumentation/SanitizerCoverageNoSafeCode.h
cp $WORKING_INCLUDE_NEW_PASS/Transforms/Instrumentation/SanitizerCoverageNoSafeCode.h $LLVM_INCLUDE_NEW_PASS/Transforms/Instrumentation/

# (3) src/llvm-project/llvm/lib/Transforms/Instrumentation/CMakeLists.txt
if [ -f $LLVM_DIR_NEW_PASS/Transforms/Instrumentation/CMakeLists.txt ]; then
    echo "[Removing old CMakeLists.txt file]"
    rm $LLVM_DIR_NEW_PASS/Transforms/Instrumentation/CMakeLists.txt
fi
cp $WORKING_DIR_NEW_PASS/Transforms/Instrumentation/CMakeLists.txt $LLVM_DIR_NEW_PASS/Transforms/Instrumentation/

# (4) src/llvm-project/llvm/lib/Passes/PassRegistry.def
if [ -f $LLVM_DIR_NEW_PASS/Passes/PassRegistry.def ]; then
    echo "[Removing old PassRegistry.def file]"
    rm $LLVM_DIR_NEW_PASS/Passes/PassRegistry.def
fi
cp $WORKING_DIR_NEW_PASS/Passes/PassRegistry.def $LLVM_DIR_NEW_PASS/Passes/

# (5) src/llvm-project/llvm/lib/Passes/PassBuilder.cpp
if [ -f $LLVM_DIR_NEW_PASS/Passes/PassBuilder.cpp ]; then
    echo "[Removing old PassBuilder.cpp file]"
    rm $LLVM_DIR_NEW_PASS/Passes/PassBuilder.cpp
fi
cp $WORKING_DIR_NEW_PASS/Passes/PassBuilder.cpp $LLVM_DIR_NEW_PASS/Passes/

# (6) src/llvm-project/llvm/include/llvm/InitializePasses.h
if [ -f $LLVM_INCLUDE_NEW_PASS/InitializePasses.h ]; then
    echo "[Removing old InitializePasses.h file]"
    rm $LLVM_INCLUDE_NEW_PASS/InitializePasses.h
fi
cp $WORKING_INCLUDE_NEW_PASS/InitializePasses.h $LLVM_INCLUDE_NEW_PASS/

# (7) src/llvm-project/llvm/lib/Transforms/Instrumentation/instrumentation.cpp
if [ -f $LLVM_DIR_NEW_PASS/Transforms/Instrumentation/Instrumentation.cpp ]; then
    echo "[Removing old Instrumentation.cpp file]"
    rm $LLVM_DIR_NEW_PASS/Transforms/Instrumentation/Instrumentation.cpp
fi
cp $WORKING_DIR_NEW_PASS/Transforms/Instrumentation/Instrumentation.cpp $LLVM_DIR_NEW_PASS/Transforms/Instrumentation/


# build rust compiler
echo "[Build Rust Compiler]"
cd $RUST
if [ -f build/x86_64-unknown-linux-gnu/llvm/llvm-finished-building ]; then
    echo "[Removing old llvm-finished-building file]"
    rm build/x86_64-unknown-linux-gnu/llvm/llvm-finished-building
fi
python3 ./x.py build --stage 1 -j `nproc`
echo "[Build Finished]"