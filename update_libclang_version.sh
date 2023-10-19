#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
VERSION=16.0.6

pushd $SCRIPT_DIR
python3 -m venv venv
source venv/bin/activate
pip install gh-folder-download
pip install git+https://github.com/edimetia3d/pybind11_weaver.git@main
rm -rf ${SCRIPT_DIR}/c_src/include/*
gh-folder-download --url https://github.com/llvm/llvm-project/tree/llvmorg-${VERSION}/clang/include/clang-c --output ${SCRIPT_DIR}/c_src/include --force
pybind11_weaver --config ${SCRIPT_DIR}/c_src/cfg.yaml

sed -i 's/__CLANG_VERSION__ = "[0-9]*\.[0-9]*\.[0-9]*"/__CLANG_VERSION__ = "'${VERSION}'"/g' pylibclang/__init__.py
sed -i 's/libclang==[0-9]*\.[0-9]*\.[0-9]*/libclang=='${VERSION}'/g' pyproject.toml