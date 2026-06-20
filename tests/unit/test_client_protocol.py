"""
Tests for MicrodatawigglerClient protocol correctness using an in-process gRPC server.
No hardware required.
"""
import struct
import threading
import warnings
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
        self._record_vars: list[tuple[int, int]] = []
        self._record_samples: list[dict] = []
        self._recording = False
        self._capped = False
        self._max_samples = 0
        # Samples injected via inject_samples() are transferred on stop_record
        # so they aren't cleared by the start_record handler.
        self._pending_samples: list[dict] = []
        self._pending_capped: bool = False

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
                elif req.HasField("start_record"):
                    if self._recording:
                        resp.error.message = "Recording already active"
                    else:
                        self._record_vars = [
                            (v.address, v.length) for v in req.start_record.variables
                        ]
                        self._max_samples = req.start_record.max_samples or 1_000_000
                        self._record_samples = []
                        self._capped = False
                        self._recording = True
                        resp.write.SetInParent()
                elif req.HasField("stop_record"):
                    if not self._recording:
                        resp.error.message = "No recording active"
                    else:
                        self._recording = False
                        if self._pending_samples:
                            self._record_samples = self._pending_samples
                            self._capped = self._pending_capped
                            self._pending_samples = []
                        resp.write.SetInParent()
                elif req.HasField("fetch_record"):
                    if self._recording:
                        resp.error.message = "Recording still active"
                    else:
                        rec = resp.record_data
                        for s in self._record_samples:
                            sample = rec.samples.add()
                            sample.timestamp_us = s["timestamp_us"]
                            for d in s["data"]:
                                sample.data.append(d)
                        rec.capped = self._capped
                else:
                    resp.error.message = "no request set"
            except Exception as e:
                resp.error.message = str(e)
            yield resp

    def inject_samples(self, samples: list[dict], *, capped: bool = False) -> None:
        """Schedule samples to be loaded into the buffer when stop_record is received."""
        self._pending_samples = samples
        self._pending_capped = capped


@pytest.fixture
def fake_server():
    store = _MemoryStore()
    servicer = _FakeMemoryServicer(store)
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
    microdatawiggler_pb2_grpc.add_MemoryServiceServicer_to_server(servicer, server)
    port = server.add_insecure_port("localhost:0")
    server.start()
    yield f"localhost:{port}", store, servicer
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
# Read / write tests
# ---------------------------------------------------------------------------

def test_write_bytes_and_read_bytes(fake_server):
    addr, _, _ = fake_server
    with MicrodatawigglerClient(addr) as c:
        c.write_bytes(0x20000000, b"\xDE\xAD\xBE\xEF")
        assert c.read_bytes(0x20000000, 4) == b"\xDE\xAD\xBE\xEF"


def test_write_and_read_uint32(fake_server):
    addr, _, _ = fake_server
    with MicrodatawigglerClient(addr) as c:
        c.write(0x20000000, 0xDEADBEEF, "uint32")
        assert c.read(0x20000000, "uint32") == 0xDEADBEEF


def test_write_and_read_int32_negative(fake_server):
    addr, _, _ = fake_server
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
    addr, _, _ = fake_server
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
    addr, _, _ = fake_server
    with MicrodatawigglerClient(addr) as c:
        c.write(0x20000000, 1, "uint8")
        c.write(0x20000001, 2, "uint8")
        c.write(0x20000002, 3, "uint8")
        assert c.read(0x20000000, "uint8") == 1
        assert c.read(0x20000001, "uint8") == 2
        assert c.read(0x20000002, "uint8") == 3


# ---------------------------------------------------------------------------
# Recording tests
# ---------------------------------------------------------------------------

def _make_sample(timestamp_us: int, values: list[bytes]) -> dict:
    return {"timestamp_us": timestamp_us, "data": values}


def test_start_stop_recording_succeeds(fake_server):
    addr, _, servicer = fake_server
    with MicrodatawigglerClient(addr) as c:
        c.start_recording_addrs(
            [{"address": 0x20000000, "length": 4, "dtype": "uint32", "name": "x"}],
            sample_rate_hz=1000,
        )
        c.stop_recording()


def test_fetch_recording_returns_list_of_dicts(fake_server):
    addr, _, servicer = fake_server
    raw = struct.pack("<I", 42)
    servicer.inject_samples([
        _make_sample(0,    [raw]),
        _make_sample(1000, [raw]),
        _make_sample(2000, [raw]),
    ])
    with MicrodatawigglerClient(addr) as c:
        c.start_recording_addrs(
            [{"address": 0x20000000, "length": 4, "dtype": "uint32", "name": "counter"}],
            sample_rate_hz=1000,
        )
        c.stop_recording()
        results = c.fetch_recording()

    assert len(results) == 3
    assert results[0] == {"timestamp_ms": 0.0, "counter": 42}
    assert results[1] == {"timestamp_ms": 1.0, "counter": 42}
    assert results[2] == {"timestamp_ms": 2.0, "counter": 42}


def test_fetch_recording_multiple_variables(fake_server):
    addr, _, servicer = fake_server
    float_raw = struct.pack("<f", 3.14)
    uint_raw = struct.pack("<I", 99)
    servicer.inject_samples([
        _make_sample(0, [float_raw, uint_raw]),
    ])
    with MicrodatawigglerClient(addr) as c:
        c.start_recording_addrs([
            {"address": 0x20000000, "length": 4, "dtype": "float",  "name": "sig"},
            {"address": 0x20000004, "length": 4, "dtype": "uint32", "name": "cnt"},
        ], sample_rate_hz=1000)
        c.stop_recording()
        results = c.fetch_recording()

    assert len(results) == 1
    assert results[0]["sig"] == pytest.approx(3.14, rel=1e-5)
    assert results[0]["cnt"] == 99
    assert results[0]["timestamp_ms"] == pytest.approx(0.0)


def test_fetch_recording_capped_emits_warning(fake_server):
    addr, _, servicer = fake_server
    raw = struct.pack("<I", 0)
    servicer.inject_samples([_make_sample(0, [raw])], capped=True)

    with MicrodatawigglerClient(addr) as c:
        c.start_recording_addrs(
            [{"address": 0x20000000, "length": 4, "dtype": "uint32", "name": "x"}],
            sample_rate_hz=1000,
            max_samples=1,
        )
        c.stop_recording()
        with warnings.catch_warnings(record=True) as w:
            warnings.simplefilter("always")
            c.fetch_recording()
        assert len(w) == 1
        assert "max_samples" in str(w[0].message)


def test_start_recording_without_elf_raises(fake_server):
    addr, _, _ = fake_server
    with MicrodatawigglerClient(addr) as c:
        with pytest.raises(RuntimeError, match="No ELF file"):
            c.start_recording(["some_var"])


def test_stop_without_recording_raises(fake_server):
    addr, _, _ = fake_server
    with MicrodatawigglerClient(addr) as c:
        with pytest.raises(RuntimeError):
            c.stop_recording()


def test_fetch_while_recording_raises(fake_server):
    addr, _, servicer = fake_server
    with MicrodatawigglerClient(addr) as c:
        c.start_recording_addrs(
            [{"address": 0x20000000, "length": 4, "dtype": "uint32", "name": "x"}],
            sample_rate_hz=1000,
        )
        with pytest.raises(RuntimeError):
            c.fetch_recording()
        c.stop_recording()


def test_write_interleaved_with_recording(fake_server):
    addr, store, servicer = fake_server
    with MicrodatawigglerClient(addr) as c:
        c.start_recording_addrs(
            [{"address": 0x20000000, "length": 4, "dtype": "uint32", "name": "x"}],
            sample_rate_hz=1000,
        )
        c.write(0x20000004, 7, "uint32")
        c.stop_recording()

    assert store.read(0x20000004, 4) == struct.pack("<I", 7)


def test_fetch_recording_timestamp_ms_conversion(fake_server):
    addr, _, servicer = fake_server
    raw = struct.pack("<I", 0)
    servicer.inject_samples([
        _make_sample(500,    [raw]),   # 500 µs → 0.5 ms
        _make_sample(1500,   [raw]),   # 1500 µs → 1.5 ms
    ])
    with MicrodatawigglerClient(addr) as c:
        c.start_recording_addrs(
            [{"address": 0x20000000, "length": 4, "dtype": "uint32", "name": "x"}],
            sample_rate_hz=1000,
        )
        c.stop_recording()
        results = c.fetch_recording()

    assert results[0]["timestamp_ms"] == pytest.approx(0.5)
    assert results[1]["timestamp_ms"] == pytest.approx(1.5)
