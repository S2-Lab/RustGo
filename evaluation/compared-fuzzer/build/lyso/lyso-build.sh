#!/bin/bash

# immediately stop building if error occurs
set -euo pipefail

LYSO_HOME="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LYSO_SOURCE_REPO="https://github.com/xiaobaozidi/Lysofuzzing.git"
MAGMA_DIR="$LYSO_HOME/magma"

if [[ ! -d "$MAGMA_DIR" ]]; then
    clone_dir="$(mktemp -d)"
    trap 'rm -rf "$clone_dir"' EXIT

    echo "[*] Cloning Lysofuzzing source"
    git clone --depth 1 "$LYSO_SOURCE_REPO" "$clone_dir/Lysofuzzing"

    if [[ ! -d "$clone_dir/Lysofuzzing/magma_dgf" ]]; then
        echo "[-] magma_dgf was not found in $LYSO_SOURCE_REPO" >&2
        exit 1
    fi

    echo "[*] Installing magma_dgf as $MAGMA_DIR"
    cp -a "$clone_dir/Lysofuzzing/magma_dgf" "$MAGMA_DIR"
    rm -rf "$clone_dir"
    trap - EXIT
else
    echo "[*] Reusing existing $MAGMA_DIR"
fi

# install prerequisite package
echo "build lyso"

cd "$LYSO_HOME"

export LYSO_HOME
export LYSO_WORKING_DIR_LLVM=$LYSO_HOME/lyso_working_directory/llvmpass
export LYSO_WORKING_DIR_REPO=$LYSO_HOME/lyso_working_directory/repo
export LYSO_WORKING_DIR_SCRIPTS=$LYSO_HOME/lyso_working_directory/scripts

# unzip magma.zip
export LYSO_DIR=$LYSO_HOME/magma/fuzzers/lyso
export LYSO_DIR_GRAPH=$LYSO_DIR/llvmpass/GraphPass
export LYSO_DIR_REPO=$LYSO_DIR/repo/llvm_mode
export LYSO_DIR_SCRIPT=$LYSO_DIR/scripts


rm -f "$LYSO_DIR_GRAPH/CFGReachabilityAnalysis.cc" \
      "$LYSO_DIR_GRAPH/CallGraph.cc" \
      "$LYSO_DIR_SCRIPT/distance_cg.py" \
      "$LYSO_DIR_SCRIPT/distance_cfg.py" \
      "$LYSO_DIR_REPO/afl-llvm-pass.so.cc"
cp -r "$LYSO_WORKING_DIR_LLVM/." "$LYSO_DIR_GRAPH"
cp "$LYSO_WORKING_DIR_REPO/afl-llvm-pass.so.cc" "$LYSO_DIR_REPO"
cp -r "$LYSO_WORKING_DIR_SCRIPTS/." "$LYSO_DIR_SCRIPT"

cd "$LYSO_DIR"

if [[ ! -x "$LYSO_DIR/llvm-11/bin/llvm-config" ]]; then
    archive="clang+llvm-11.0.1-x86_64-linux-gnu-ubuntu-16.04.tar.xz"
    mkdir -p llvm-11
    wget "https://github.com/llvm/llvm-project/releases/download/llvmorg-11.0.1/$archive"
    tar -xf "$archive" -C llvm-11 --strip-components=1
    rm -f "$archive"
fi

export PATH=$PWD/llvm-11/bin:$PATH
export LLVM_CONFIG=$PWD/llvm-11/bin/llvm-config
export CC=$PWD/llvm-11/bin/clang
export CXX=$PWD/llvm-11/bin/clang++

# CMake 4 removed compatibility with projects that declare a minimum version
# below 3.5. Lyso's LLVM pass is otherwise compatible, so raise only the legacy
# CMake policy floor in the checked-out third-party source.
find "$LYSO_DIR" -name CMakeLists.txt -exec sed -i -E \
    's/cmake_minimum_required\(VERSION 3\.[0-4](\.[0-9]+)?\)/cmake_minimum_required(VERSION 3.5)/' {} +

pushd llvmpass
./compile_graphpass.sh $PWD/../llvm-11/bin
popd

pushd repo
make clean
make
make -C llvm_mode clean
make -C llvm_mode LLVM_CONFIG="$LLVM_CONFIG" CC="$CC" CXX="$CXX"
ar rcs libafl-llvm-rt.a afl-llvm-rt.o
popd
