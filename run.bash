#!/bin/bash
# Simple runner script

set -e

DOCKER_RUN="docker run --rm --volume $PWD:/workspace --user $(id -u):$(id -g)"
CONAN_CACHE="$PWD/conan_cache"
mkdir -p "$CONAN_CACHE"
DOCKER_RUN_CONAN="$DOCKER_RUN --volume $CONAN_CACHE:/home/builduser/.conan2"
IMAGE="microdatawiggler-development"

case "$1" in
  build-container)
    docker build --tag "$IMAGE" ./images/development
    ;;
  build-debug)
    $DOCKER_RUN_CONAN "$IMAGE" bash -c '
      conan profile detect
      conan install . --build=missing -s build_type=Debug
      . ./build/Debug/generators/conanbuild.sh
      cmake --preset conan-debug
      cmake --build --preset conan-debug
    '
    ;;
  build-python-proto)
    $DOCKER_RUN "$IMAGE" bash -c '
      OUT=python/microdatawiggler/src/microdatawiggler
      python3 -m grpc_tools.protoc \
        -I microdatawiggler/proto \
        --python_out="$OUT" \
        --grpc_python_out="$OUT" \
        microdatawiggler/proto/microdatawiggler.proto
      sed -i "s/^import microdatawiggler_pb2/from . import microdatawiggler_pb2/" "$OUT/microdatawiggler_pb2_grpc.py"
    '
    ;;
  build-validation-firmware)
    $DOCKER_RUN_CONAN "$IMAGE" bash -c '
      mkdir -p firmware_build
      cd firmware_build
      cmake ../validation_firmware
      cmake --build .
    '
    ;;
  flash-simple-firmware)
    STM32_Programmer_CLI -c port=SWD -w firmware_build/simple.elf -hardRst
    ;;
  unit-tests)
    $DOCKER_RUN_CONAN "$IMAGE" bash -c '
      . ./build/Debug/generators/conanbuild.sh
      ctest --output-on-failure --preset conan-debug -L unit
    '
    ;;
  integration-tests)
    $DOCKER_RUN_CONAN --privileged -v /dev/bus/usb:/dev/bus/usb "$IMAGE" bash -c '
      . ./build/Debug/generators/conanbuild.sh
      ctest --output-on-failure --parallel 1 --preset conan-debug -L integration
    '
    ;;
  fix-formatting)
    $DOCKER_RUN "$IMAGE" bash -c '
      find microdatawiggler -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i
    '
    ;;
  check-formatting)
    $DOCKER_RUN "$IMAGE" bash -c '
      find microdatawiggler -name "*.cpp" -o -name "*.hpp" | xargs clang-format --Werror --dry-run
    '
    ;;
  check-tidy)
    $DOCKER_RUN_CONAN "$IMAGE" bash -c '
      run-clang-tidy -p build/Debug -header-filter="microdatawiggler/inc/.*" microdatawiggler/src/
    '
    ;;
  doxygen)
    $DOCKER_RUN "$IMAGE" doxygen Doxyfile
    ;;
  package-deb)
    $DOCKER_RUN_CONAN "$IMAGE" bash -c '
      cd build/Debug
      cmake --install . --config Debug --prefix _install
      if [ -n "${PACKAGE_VERSION}" ]; then
        cpack -G DEB -D "CPACK_PACKAGE_VERSION=${PACKAGE_VERSION}"
      else
        cpack -G DEB
      fi
    '
    ;;
  build-microdatawiggler-container)
    docker build --tag microdatawiggler --file images/microdatawiggler/Dockerfile .
    ;;
  build-python-packages)
    $DOCKER_RUN -e "PACKAGE_VERSION=${PACKAGE_VERSION:-}" "$IMAGE" bash -c '
      python3 -m build --wheel "python/microdatawiggler"
    '
    ;;
  run)
    ./build/Debug/microdatawiggler/microdatawiggler
    ;;
  example-client)
    docker run --rm --network host \
      --privileged \
      -v /dev/bus/usb:/dev/bus/usb \
      --volume "$PWD/examples":/examples \
      --volume "$PWD/firmware_build":/elf_files \
      --entrypoint python3 \
      microdatawiggler \
      /examples/example_client.py
    ;;
  pytest-tests)
    $DOCKER_RUN "$IMAGE" bash -c '
      PYTHONPATH=python/microdatawiggler/src \
        python3 -m pytest tests/unit/
    '
    ;;
  hardware-tests)
    $DOCKER_RUN --privileged --network host -v /dev/bus/usb:/dev/bus/usb "$IMAGE" bash -c '
      PYTHONPATH=python/microdatawiggler/src \
        python3 -m pytest tests/hardware/ --elf-path firmware_build/simple.elf
    '
    ;;
  all)
    ./run.bash build-container
    ./run.bash build-debug
    ./run.bash check-formatting
    ./run.bash check-tidy
    ./run.bash doxygen
    ./run.bash unit-tests
    ./run.bash package-deb
    ./run.bash build-python-proto
    ./run.bash build-python-packages
    ./run.bash build-validation-firmware
    ./run.bash flash-simple-firmware
    ./run.bash integration-tests
    ./run.bash pytest-tests
    ./run.bash build-microdatawiggler-container
    ./run.bash example-client
    ;;
  *)
    echo "Usage: ./run.bash {build-container|build-debug|unit-tests|integration-tests|pytest-tests|hardware-tests|check-formatting|check-tidy|doxygen|package-deb|build-microdatawiggler-container|build-python-packages|run|example-client|all}"
    exit 1
    ;;
esac
