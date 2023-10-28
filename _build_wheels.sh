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
