#/!/bin/bash
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]:-$0}"; )" &> /dev/null && pwd 2> /dev/null; )";
cd $SCRIPT_DIR
rm -rf *.egg-info
rm -rf dist
rm -rf build
rm -rf dist
rm -rf venv
rm -rf pylibclang-stubs

python3 -m venv venv
source venv/bin/activate
pip install ../ --upgrade
pip install pybind11-stubgen --upgrade
pybind11-stubgen pylibclang._C -o $PWD/tmp --ignore-unresolved-names capsule
mv tmp/pylibclang/* pylibclang-stubs/
rm -rf tmp
python3 -m pip install --upgrade build twine setuptools wheel
python3 -m build --no-isolation

