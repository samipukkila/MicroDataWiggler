"""
Tests for MicrodatawigglerClient protocol correctness using an in-process gRPC server.
No hardware required.
"""
import threading
from concurrent import futures

import grpc
import pytest

from microdatawiggler import MicrodatawigglerClient
from microdatawiggler import microdatawiggler_pb2, microdatawiggler_pb2_grpc


# ---------------------------------------------------------------------------
# Minimal in-process server
# ---------------------------------------------------------------------------

class _MemoryStore:
    """Simple byte-addressable store backing the fake server."""
    def __init__(self):
        self._mem: dict[int, int] = {}

    def write(self, address: int, data: bytes) -> None:
        for i, b in enumerate(data):
            self._mem[address + i] = b

    def read(self, address: int, length: int) -> bytes:
        return bytes(self._mem.get(address + i, 0) for i in range(length))


class _FakeMemoryServicer(microdatawiggler_pb2_grpc.MemoryServiceServicer):
    def __init__(self, store: _MemoryStore, *, raise_on_address: int | None = None):
        self._store = store
        self._raise_on_address = raise_on_address

    def Session(self, request_iterator, context):
        for req in request_iterator:
            resp = microdatawiggler_pb2.MemoryResponse()
            try:
                if req.HasField("read"):
                    if self._raise_on_address == req.read.address:
                        raise RuntimeError("simulated probe error")
                    data = self._store.read(req.read.address, req.read.length)
                    resp.read.data = data
                elif req.HasField("write"):
                    if self._raise_on_address == req.write.address:
                        raise RuntimeError("simulated probe error")
                    self._store.write(req.write.address, req.write.data)
                    resp.write.SetInParent()
                else:
                    resp.error.message = "no request set"
            except Exception as e:
                resp.error.message = str(e)
            yield resp


@pytest.fixture
def fake_server():
    store = _MemoryStore()
    servicer = _FakeMemoryServicer(store)
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
    microdatawiggler_pb2_grpc.add_MemoryServiceServicer_to_server(servicer, server)
    port = server.add_insecure_port("localhost:0")
    server.start()
    yield f"localhost:{port}", store
    server.stop(grace=0)


@pytest.fixture
def fake_server_with_error():
    store = _MemoryStore()
    servicer = _FakeMemoryServicer(store, raise_on_address=0xDEAD0000)
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
    microdatawiggler_pb2_grpc.add_MemoryServiceServicer_to_server(servicer, server)
    port = server.add_insecure_port("localhost:0")
    server.start()
    yield f"localhost:{port}"
    server.stop(grace=0)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_write_bytes_and_read_bytes(fake_server):
    addr, _ = fake_server
    with MicrodatawigglerClient(addr) as c:
        c.write_bytes(0x20000000, b"\xDE\xAD\xBE\xEF")
        assert c.read_bytes(0x20000000, 4) == b"\xDE\xAD\xBE\xEF"


def test_write_and_read_uint32(fake_server):
    addr, _ = fake_server
    with MicrodatawigglerClient(addr) as c:
        c.write(0x20000000, 0xDEADBEEF, "uint32")
        assert c.read(0x20000000, "uint32") == 0xDEADBEEF


def test_write_and_read_int32_negative(fake_server):
    addr, _ = fake_server
    with MicrodatawigglerClient(addr) as c:
        c.write(0x20000004, -1, "int32")
        assert c.read(0x20000004, "int32") == -1


@pytest.mark.parametrize("dtype,value", [
    ("int8",   -100),
    ("uint8",  200),
    ("int16",  -1000),
    ("uint16", 60000),
    ("int32",  -100000),
    ("uint32", 3000000000),
    ("int64",  -(2**60)),
    ("uint64", 2**60),
    ("float",  3.14),
    ("double", -1.23456789),
    ("bool",   True),
])
def test_typed_roundtrip(fake_server, dtype, value):
    addr, _ = fake_server
    with MicrodatawigglerClient(addr) as c:
        c.write(0x20000000, value, dtype)
        result = c.read(0x20000000, dtype)
        if dtype in ("float", "double"):
            assert result == pytest.approx(value, rel=1e-5)
        else:
            assert result == value


def test_read_error_raises_runtime_error(fake_server_with_error):
    with MicrodatawigglerClient(fake_server_with_error) as c:
        with pytest.raises(RuntimeError, match="simulated probe error"):
            c.read_bytes(0xDEAD0000, 4)


def test_write_error_raises_runtime_error(fake_server_with_error):
    with MicrodatawigglerClient(fake_server_with_error) as c:
        with pytest.raises(RuntimeError, match="simulated probe error"):
            c.write_bytes(0xDEAD0000, b"\x00\x00\x00\x00")


def test_not_connected_raises():
    c = MicrodatawigglerClient()
    with pytest.raises(RuntimeError, match="Not connected"):
        c.read_bytes(0x20000000, 4)


def test_multiple_requests_in_sequence(fake_server):
    addr, _ = fake_server
    with MicrodatawigglerClient(addr) as c:
        c.write(0x20000000, 1, "uint8")
        c.write(0x20000001, 2, "uint8")
        c.write(0x20000002, 3, "uint8")
        assert c.read(0x20000000, "uint8") == 1
        assert c.read(0x20000001, "uint8") == 2
        assert c.read(0x20000002, "uint8") == 3
