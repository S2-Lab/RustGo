#!/bin/bash

# immediately stop building if error occurs
set -e

# check if clang-11 is installed
echo "##Check if clang-11 && clang++-11 is installed##"
echo "`which clang-11` `clang-11 --version`"

# Environmental Variables
export LLVM11_DIR=/usr/lib/llvm-11
export LD_LIBRARY_PATH=$LLVM11_DIR/lib:${LD_LIBRARY_PATH:-}
export PATH=$LLVM11_DIR/bin:$PATH
export CC=$LLVM11_DIR/bin/clang
export CXX=$LLVM11_DIR/bin/clang++
export LLVM_CONFIG=$LLVM11_DIR/bin/llvm-config
export AFLGO_WORKING_DIR=$PWD/aflgo_working_directory
export DISTANCE=$AFLGO_WORKING_DIR/distance_calculator
export SCRIPTS=$AFLGO_WORKING_DIR/scripts

# build AFL 
echo "##Build AFLGo##"
git clone https://github.com/aflgo/aflgo.git
export AFLGO=$PWD/aflgo
cd aflgo
git reset --hard b532e1f

echo "##Copy distance_calculator/main.cpp and scripts/gen_distance_fast.py##"
rm distance_calculator/main.cpp && cp $DISTANCE/main.cpp distance_calculator/main.cpp
rm scripts/gen_distance_fast.py && cp $SCRIPTS/gen_distance_fast.py scripts/gen_distance_fast.py

echo "##Build AFLGo##"
make clean all && cd llvm_mode && make clean all

echo "##Build distance_calculator##"
cd $AFLGO/distance_calculator && cmake -G Ninja ./ && cmake --build ./ && cd $AFLGO && ar rcs libafl-llvm-rt.a afl-llvm-rt.o

echo "##Build Finished##"
