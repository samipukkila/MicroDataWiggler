# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Microdatawiggler** is a gRPC-based tool for reading and writing firmware variables at runtime via an ST-Link debug probe. A C++ server connects to the target over SWD and exposes a raw memory read/write gRPC API. A Python client library resolves symbol names to addresses and types using DWARF debug info from the ELF file, then issues typed reads/writes through the gRPC session.

## Build & Development Commands

All development happens inside Docker. Use `run.bash` for all tasks:

```bash
./run.bash build-container               # Build Docker dev image (do first)
./run.bash build-debug                   # CMake + Conan build (Debug)
./run.bash unit-tests                    # Run GTest unit tests (no hardware)
./run.bash integration-tests             # Run C++ integration tests (requires ST-Link + target)
./run.bash check-formatting              # Verify clang-format compliance
./run.bash fix-formatting                # Auto-fix clang-format issues
./run.bash check-tidy                    # Run clang-tidy static analysis
./run.bash build-python-proto            # Regenerate Python gRPC stubs from .proto
./run.bash build-python-packages         # Build Python wheel (output: python/microdatawiggler/dist/)
./run.bash package-deb                   # Build .deb package (output: build/Debug/*.deb)
./run.bash build-microdatawiggler-container  # Build production Docker image
./run.bash pytest-tests                  # Run Python unit tests (no hardware)
./run.bash hardware-tests                # Run Python hardware tests (requires ST-Link + target)
./run.bash example-client                # Run example_client.py in the production container
./run.bash run                           # Run server binary directly on the host
```

To run a single GTest binary after building:
```bash
# Inside the container, from /build/Debug/
./unit_tests/microdatawiggler_unit_tests --gtest_filter=StlinkProbeTest.*
```

## Architecture

### C++ Server (`microdatawiggler/`)

Two collaborating subsystems, injected via interfaces:

1. **DebugProbe** (`inc/debug_probe.hpp`) — abstract interface for SWD memory access. `StlinkProbe` wraps libstlink to connect to the target and perform raw `read_bytes` / `write_bytes`.

2. **MemoryServiceImpl** (`inc/memory_service.hpp`, `src/memory_service.cpp`) — gRPC bidirectional streaming service implementing `MemoryService::Session`. Dispatches `ReadRequest` and `WriteRequest` messages directly to the probe. Enforces a single-session mutex — only one client may connect at a time.

The `.proto` file lives at `microdatawiggler/proto/microdatawiggler.proto`. The service is intentionally type-agnostic: it operates on raw bytes only. All symbol resolution and type knowledge live in the Python client.

Server accepts one optional flag: `microdatawiggler [--port PORT]` (default 50051).

### Python Package (`python/microdatawiggler/`)

One pip-installable package (`microdatawiggler`). Install for development with `pip install -e python/microdatawiggler`.

| Module        | Purpose                                                                                                    |
|---------------|------------------------------------------------------------------------------------------------------------|
| `_client.py`  | `MicrodatawigglerClient` — gRPC client with named-variable and address-based APIs                          |
| `_elf.py`     | `ElfSymbolResolver` — parses DWARF debug info from an ELF to resolve variable names to address/dtype/size  |
| `_types.py`   | Pack/unpack helpers for all supported dtypes (`int8`–`int64`, `uint8`–`uint64`, `float`, `double`, `bool`) |
| `_process.py` | `MicrodatawigglerLauncher()` — spawns the server binary and polls until gRPC is ready                      |

`MicrodatawigglerClient` APIs:

```python
# Named-variable API — requires elf_path
with MicrodatawigglerClient("localhost:50051", elf_path="firmware.elf") as client:
    client.write_variable("in_uint32_t", 42)
    value = client.read_variable("out_uint32_t")
    info = client.get_variable_info("cnt_float")  # {"address": ..., "type": ..., "size": ..., "count": ...}

# Address-based API — no ELF needed
with MicrodatawigglerClient("localhost:50051") as client:
    client.write(0x20000000, 42, "uint32")
    value = client.read(0x20000000, "uint32")
    raw = client.read_bytes(0x20000100, 8)
    client.write_bytes(0x20000100, b"\x00" * 8)
```

Proto stubs (`microdatawiggler_pb2.py`, `microdatawiggler_pb2_grpc.py`) live inside `python/microdatawiggler/src/microdatawiggler/`. Regenerate with `./run.bash build-python-proto`.

### Example Firmware (`validation_firmware/`)

STM32 Nucleo F446RE project. `validation_firmware/simple/firmware.c` defines:
- `in_<type>` / `out_<type>` variable pairs — firmware copies `in_*` → `out_*` every 1 ms.
- `cnt_<type>` counters — incremented every 100 ms.
- `led_on` — controls the on-board LED.

Build: `./run.bash build-validation-firmware` → `firmware_build/simple.elf`
Flash: `./run.bash flash-simple-firmware`

### Docker Images

| Directory                  | Image tag                      | Purpose                                                         |
|----------------------------|--------------------------------|-----------------------------------------------------------------|
| `images/development/`      | `microdatawiggler-development` | Dev toolchain (CMake, Conan, GCC, clang-tools, Python)          |
| `images/microdatawiggler/` | `microdatawiggler`             | Production image — server from `.deb`, Python client from wheel |

## Testing Strategy

- **C++ unit tests** (`microdatawiggler/unit_tests/`) — GTest + FFF mocks for libstlink C functions and C++ interfaces. No hardware required.
- **C++ integration tests** (`microdatawiggler/integration_tests/`) — require a physical ST-Link probe and a flashed target.
- **Python unit tests** (`tests/unit/`) — test `MicrodatawigglerClient` protocol against an in-process fake gRPC server; test `ElfSymbolResolver` against fixture ELF files; test type pack/unpack. No hardware.
- **Python hardware tests** (`tests/hardware/`) — launch the real server via `MicrodatawigglerLauncher()` and connect to a running target. Accept `--elf-path` (default: `firmware_build/simple.elf`). Require ST-Link + flashed target.

## Key Conventions

- **C++20**, no compiler extensions.
- Dependency injection via abstract interfaces; `MemoryServiceImpl` takes `DebugProbe&`, not a concrete type.
- FFF for mocking C-linkage libstlink functions; GTest mocks for C++ interfaces.
- clang-format and clang-tidy are CI gates — run `check-formatting` and `check-tidy` before pushing.
- gRPC and protobuf come from Ubuntu system packages (`libgrpc++-dev`, `libprotobuf-dev`); found in CMake via `pkg_check_modules`.
- Conan 2.x is used only for GTest and fff (test-only deps).
- The server is deliberately type-agnostic — it never interprets memory contents. All DWARF parsing and struct packing live in the Python client.
