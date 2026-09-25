#!/bin/bash

# immediately stop building if error occurs
set -e

# check if clang-12 is installed
echo "##Check if clang-12 && clang++-12 is installed##"
echo "`which clang-12` `clang-12 --version`"

# Environmental Variables
export CC=clang-12
export CXX=clang++-12
export LLVM_CONFIG=llvm-config-12
export AFL_REAL_LD=ld.lld

# build AFL 
echo "##Build AFL##"
git clone https://github.com/google/AFL.git
export AFL=$PWD/AFL
cd AFL
make clean all && cd llvm_mode && make clean all
cd ../ && ar rcs libafl-llvm-rt.a afl-llvm-rt.o
