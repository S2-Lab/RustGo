#!/bin/bash

# immediately stop building if error occurs
set -Eeuo pipefail
# set environment with s2fuzz compiler
export CURR=$PWD
export ENV_DIR=$CURR/../env
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
pushd $ENV_DIR
source env.sh
popd

# install prerequisite package
echo "build SVF & s2pidy"

# set LLVM and Rust compiler

export s2pidy_dir=$PWD/../src/SVF-s2pidy
export S2HOME=`pwd`/../
# install SVF - commit : 
rm -rf SVF
git clone https://github.com/SVF-tools/SVF.git
export SVF=$PWD/SVF
cd SVF
export SVF_INC=$PWD/include
export SVF_LIB=$PWD/lib
export SVF_TOOLS=$PWD/tools

git reset --hard 54bbfe670ce64892b1fcf43a00f6718d5dcc3428

echo "[Build rust-demangle static library]"
pushd "$s2pidy_dir/rust-demangle"
if [ -n "${CARGO:-}" ] && [ -x "$CARGO" ]; then
    "$CARGO" build
elif command -v rustup >/dev/null 2>&1 && rustup toolchain list | grep -q '^1\.69\.0'; then
    cargo +1.69.0 build
else
    cargo build
fi
test -f "$s2pidy_dir/rust-demangle/target/debug/librust_demangle.a"
popd

# set CMakeLists.txt
echo "[Start Modifying SVF CMakeLists]"
rm CMakeLists.txt
cp $s2pidy_dir/CMakeLists.txt $SVF

# set SVF include files
echo "[Start Modifying SVF libraries]"
# (1) include/Graphs
cd include/Graphs
export GraphsInc=`pwd`
rm GenericGraph.h SVFG.h PAG.h PAGEdge.h VFG.h ConsG.h ConsGEdge.h ConsGNode.h VFGNode.h
cd $s2pidy_dir/include/Graphs
cp GenericGraph.h SVFG.h PAG.h PAGEdge.h VFG.h ConsG.h ConsGEdge.h ConsGNode.h VFGNode.h $GraphsInc

# (2) include/SVF-FE
cd $SVF_INC
cd SVF-FE
export SVFFEInc=`pwd`
rm PAGBuilder.h SymbolTableInfo.h
cd $s2pidy_dir/include/SVF-FE
cp PAGBuilder.h SymbolTableInfo.h $SVFFEInc

# (3) include/S2pidy
cd $SVF_INC
mkdir S2pidy
cd S2pidy
export S2pidyInc=`pwd`
cd $s2pidy_dir/include/S2pidy
cp util.h RustDemangle.h $S2pidyInc


# set SVF lib files
cd $SVF
cd lib
export SVF_LIB=$PWD

# (1) lib/Graphs
cd $SVF_LIB
cd Graphs
export GraphLib=$PWD
rm ConsG.cpp PAG.cpp VFG.cpp SVFG.cpp
cp $s2pidy_dir/lib/Graphs/ConsG.cpp $s2pidy_dir/lib/Graphs/PAG.cpp $s2pidy_dir/lib/Graphs/VFG.cpp $s2pidy_dir/lib/Graphs/SVFG.cpp $GraphLib

# (2) lib/SVF-FE
cd $SVF_LIB
cd SVF-FE
export SVFFELib=$PWD
rm PAGBuilder.cpp SymbolTableInfo.cpp
cp $s2pidy_dir/lib/SVF-FE/PAGBuilder.cpp $s2pidy_dir/lib/SVF-FE/SymbolTableInfo.cpp $SVFFELib

# (3) lib/S2pidy
cd $SVF_LIB
mkdir S2pidy
cd S2pidy
export S2pidyLib=$PWD
cp $s2pidy_dir/lib/S2pidy/fence.cpp $s2pidy_dir/lib/S2pidy/util.cpp $S2pidyLib

# (4) lib/CMakeLists.txt
cd $SVF_LIB
rm CMakeLists.txt
cp $s2pidy_dir/lib/CMakeLists.txt $SVF_LIB


# set SVF tools files

# (1) tools/CMakeLists.txt
cd $SVF_TOOLS
rm CMakeLists.txt
cp $s2pidy_dir/tools/CMakeLists.txt $SVF_TOOLS

# (2) tools/S2pidy/CMakeLists.txt
mkdir S2pidy
cd S2pidy
export S2pidyTools=$PWD
cp $s2pidy_dir/tools/S2pidy/CMakeLists.txt $s2pidy_dir/tools/S2pidy/s2pidy.cpp $S2pidyTools

echo "[Finished Modifying SVF libraries]"

# build SVF

cd $SVF

echo "[Build SVF]"
./build.sh
cmake --build Release-build -- -j "${JOBS:-4}"
test -x "$SVF/Release-build/bin/s2pidy"
echo "[Build Finished] s2pidy: $SVF/Release-build/bin/s2pidy"
