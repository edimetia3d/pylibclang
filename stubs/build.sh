#/!/bin/bash
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]:-$0}"; )" &> /dev/null && pwd 2> /dev/null; )";
cd $SCRIPT_DIR

python3 -m venv venv
source venv/bin/activate
pip install ../ --upgrade
pip install pybind11-stubgen --upgrade
rm -rf pylibclang-stubs/*
pybind11-stubgen pylibclang._C -o $PWD/tmp --ignore-unresolved-names capsule
mv tmp/pylibclang/* pylibclang-stubs/
rm -rf tmp

rm -rf *.egg-info
rm -rf dist
python3 -m pip install --upgrade build twine setuptools wheel
python3 -m build --no-isolation

