#/!/bin/bash
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]:-$0}"; )" &> /dev/null && pwd 2> /dev/null; )";
cd $SCRIPT_DIR

PLAT=manylinux_2_28_x86_64
DOCKER_IMAGE="quay.io/pypa/${PLAT}"
docker pull $DOCKER_IMAGE
docker run --rm -i -e PLAT=${PLAT} -v `pwd`:/io $DOCKER_IMAGE /io/_build_wheels.sh
python3 -m pip install --upgrade build twine
python3 -m build -o ./wheelhouse --sdist
bash ./stubs/build.sh
cp ./stubs/dist/* ./wheelhouse

if [ "$1" = "upload" ];
  then
    python3 -m twine upload wheelhouse/*
  else
    echo Uploading ignored
fi


