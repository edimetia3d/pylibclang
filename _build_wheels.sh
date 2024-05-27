#!/bin/bash
set -e -u -x

function repair_wheel {
    wheel="$1"
    if ! auditwheel show "$wheel"; then
        echo "Skipping non-platform wheel $wheel"
    else
        auditwheel repair "$wheel" --plat "$PLAT" -w /io/wheelhouse/
    fi
}

# build llvm
bash /io/build_llvm.sh

# Compile wheels
rm -rf /io/wheelhouse/*
mkdir -p /io/wheelhouse/
chmod 777 /io/wheelhouse
cp -r /io ~/src
rm -rf /opt/python/cp36*
for PYBIN in /opt/python/cp*/bin; do
    "${PYBIN}/pip" install --upgrade build
    "${PYBIN}/python" -m build ~/src -o ~/src/wheelhouse/ -w
done

# Bundle external shared libraries into the wheels
for whl in ~/src/wheelhouse/*.whl; do
    repair_wheel "$whl"
done

# build stub
PYTHON3=/opt/python/cp311-cp311/bin/python3
pushd ~/src/stubs
$PYTHON3 -m venv stub_build_venv
source stub_build_venv/bin/activate
$PYTHON3 -m pip install --upgrade build setuptools wheel
$PYTHON3 -m pip install ../ --upgrade
$PYTHON3 -m pip install pybind11-stubgen --upgrade
/opt/python/cp311-cp311/bin/pybind11-stubgen pylibclang._C -o $PWD/tmp --ignore-unresolved-names capsule
mv tmp/pylibclang/* pylibclang-stubs/
rm -rf tmp
$PYTHON3 -m build --no-isolation
cp ./dist/* /io/wheelhouse/
deactivate
popd