#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
VERSION=18.1.1

pushd $SCRIPT_DIR


mkdir -p llvm_cache
if [ ! -f llvm_cache/llvmorg-${VERSION}.zip ]; then
    wget https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-${VERSION}.zip -O llvm_cache/llvmorg-${VERSION}.zip
    unzip llvm_cache/llvmorg-${VERSION}.zip -d llvm_cache
fi

rm -rf ${SCRIPT_DIR}/c_src/include/*
mkdir -p ${SCRIPT_DIR}/c_src/include/clang/include
cp -r llvm_cache/llvm-project-llvmorg-${VERSION}/clang/include/clang-c ${SCRIPT_DIR}/c_src/include/clang/include


python3 -m venv _update_ver_venv
source _update_ver_venv/bin/activate
pip install git+https://github.com/edimetia3d/pybind11_weaver.git@main
pybind11-weaver --config ${SCRIPT_DIR}/c_src/cfg.yaml

sed -i 's/__CLANG_VERSION__ = "[0-9]*\.[0-9]*\.[0-9]*"/__CLANG_VERSION__ = "'${VERSION}'"/g' pylibclang/__init__.py
sed -i 's/libclang==[0-9]*\.[0-9]*\.[0-9]*/libclang=='${VERSION}'/g' pyproject.toml

rm -rf _update_ver_venv
popd