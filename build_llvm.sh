#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd $SCRIPT_DIR

BUILD_DIR=$SCRIPT_DIR/llvm_cache/build
INSTALL_DIR=$SCRIPT_DIR/llvm_cache/install


mkdir -p $BUILD_DIR
rm -rf $INSTALL_DIR
mkdir -p $INSTALL_DIR

pushd $SCRIPT_DIR/llvm_cache/llvm-project-llvmorg-*
cmake llvm \
  -B $BUILD_DIR \
  -DLLVM_ENABLE_PROJECTS="clang" \
  -DLLVM_PARALLEL_LINK_JOBS=1 \
  -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
  -DCMAKE_BUILD_TYPE=Release
popd

pushd $BUILD_DIR
cmake --build . --target install-libclang --parallel 8
popd