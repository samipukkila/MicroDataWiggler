FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install --no-install-recommends -y \
    python3 \
    python3-pip \
    cmake \
    make \
    g++ \
    pkgconf \
    clang-format \
    clang-tidy \
    ninja-build \
    libstlink-dev \
    libstlink1 \
    libusb-1.0 \
    perl \
    doxygen \
    graphviz \
    gdb-multiarch \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

RUN python3 -m pip install --break-system-packages conan pytest grpcio grpcio-tools build twine pyelftools

RUN groupadd -r builduser && useradd -r -m -g builduser builduser

WORKDIR /workspace
ENV PATH="/workspace/build/Debug/microdatawiggler:${PATH}"

USER builduser
ENV CONAN_HOME=/home/builduser/.conan2
RUN conan profile detect
RUN g++ --version && cmake --version && python3 --version && gcc --version
RUN chmod -R o+rwx /home/builduser
