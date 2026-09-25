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

# build WindRanger
echo "##Build WindRanger##"
if [ ! -d WindRanger ]; then
  git clone https://github.com/Mem2019/WindRanger.git
fi

export WINDRANGER=$PWD/WindRanger
# Compile WindRanger
cd WindRanger
rm -rf SVF
git clone https://github.com/SVF-tools/SVF.git
cd SVF
git reset --hard 827d587
git apply <<'PATCH'
diff --git a/lib/Graphs/SVFG.cpp b/lib/Graphs/SVFG.cpp
index 58cfdc55..2b14b20e 100644
--- a/lib/Graphs/SVFG.cpp
+++ b/lib/Graphs/SVFG.cpp
@@ -667,7 +667,7 @@ const SVFFunction* SVFG::isFunEntrySVFGNode(const SVFGNode* node) const
     else if(const InterMSSAPHISVFGNode* mphi = SVFUtil::dyn_cast<InterMSSAPHISVFGNode>(node))
     {
         if(mphi->isFormalINPHI())
-            return phi->getFun();
+            return mphi->getFun();
     }
     return nullptr;
 }
PATCH
cd $WINDRANGER

# CMake 4 refuses the old 3.4.3 policy declarations used by WindRanger/SVF.
# The build works with the same sources once the minimum compatibility floor is
# moved to 3.5.
find "$WINDRANGER" -name CMakeLists.txt -exec sed -i -E \
    's/cmake_minimum_required\(VERSION 3\.[0-4](\.[0-9]+)?\)/cmake_minimum_required(VERSION 3.5)/' {} +

make clean all && cd llvm_mode && make clean all && cd $WINDRANGER

# Should Make afl-llvm-rt static library
ar rcs libafl-llvm-rt.a afl-llvm-rt.o

echo "##Build Finished##"
