import queue
import warnings

import grpc

from . import microdatawiggler_pb2, microdatawiggler_pb2_grpc
from ._types import dtype_size, pack, unpack


class MicrodatawigglerClient:
    """gRPC client for reading and writing target memory via Microdatawiggler.

    Basic usage (address-based)::

        with MicrodatawigglerClient("localhost:50051") as client:
            client.write(0x20000000, 42, "uint32")
            value = client.read(0x20000000, "uint32")

    Named-variable usage (requires elf_path)::

        with MicrodatawigglerClient("localhost:50051", elf_path="firmware.elf") as client:
            client.write_variable("my_counter", 0)
            value = client.read_variable("my_counter")

    Recording usage::

        with MicrodatawigglerClient("localhost:50051", elf_path="firmware.elf") as client:
            client.start_recording(["cnt_float", "in_uint32_t"], sample_rate_hz=1000)
            client.write_variable("led_on", True)
            time.sleep(2.0)
            client.stop_recording()
            results = client.fetch_recording()
            # [{"timestamp_ms": 0.0, "cnt_float": 0.0, "in_uint32_t": 42}, ...]
    """

    def __init__(self, address: str = "localhost:50051", elf_path: str | None = None):
        self._address = address
        self._channel = None
        self._request_queue = None
        self._response_iter = None
        self._recording_vars: list[tuple[str, str]] = []
        if elf_path is not None:
            from ._elf import ElfSymbolResolver
            self._resolver = ElfSymbolResolver(elf_path)
        else:
            self._resolver = None

    def connect(self) -> None:
        self._channel = grpc.insecure_channel(self._address)
        stub = microdatawiggler_pb2_grpc.MemoryServiceStub(self._channel)
        self._request_queue = queue.Queue()
        self._response_iter = stub.Session(self._request_generator())

    def disconnect(self) -> None:
        if self._request_queue is not None:
            self._request_queue.put(None)
            self._request_queue = None
        self._response_iter = None
        if self._channel is not None:
            self._channel.close()
            self._channel = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.disconnect()

    # ------------------------------------------------------------------
    # Named-variable API (requires elf_path)
    # ------------------------------------------------------------------

    def read_variable(self, name: str):
        """Read a named firmware variable, returning a native Python value."""
        sym = self._require_resolver().resolve(name)
        if sym.count != 1:
            raise ValueError(
                f"'{name}' is an array ({sym.count} elements); use read_bytes() instead"
            )
        return unpack(sym.dtype, self.read_bytes(sym.address, sym.size))

    def write_variable(self, name: str, value) -> None:
        """Write a value to a named firmware variable."""
        sym = self._require_resolver().resolve(name)
        if sym.count != 1:
            raise ValueError(
                f"'{name}' is an array ({sym.count} elements); use write_bytes() instead"
            )
        self.write_bytes(sym.address, pack(sym.dtype, value))

    def get_variable_info(self, name: str) -> dict:
        """Return address, type, size, and count metadata for a named variable."""
        sym = self._require_resolver().resolve(name)
        return {
            "address": sym.address,
            "type": sym.type,
            "size": sym.size,
            "count": sym.count,
        }

    def _require_resolver(self):
        if self._resolver is None:
            raise RuntimeError(
                "No ELF file configured. Pass elf_path= to MicrodatawigglerClient()."
            )
        return self._resolver

    # ------------------------------------------------------------------
    # Address-based API
    # ------------------------------------------------------------------

    def read(self, address: int, dtype: str):
        """Read from target memory at address and return a Python value."""
        return unpack(dtype, self.read_bytes(address, dtype_size(dtype)))

    def write(self, address: int, value, dtype: str) -> None:
        """Write a Python value to target memory at address."""
        self.write_bytes(address, pack(dtype, value))

    def read_bytes(self, address: int, length: int) -> bytes:
        """Read raw bytes from target memory."""
        req = microdatawiggler_pb2.MemoryRequest(
            read=microdatawiggler_pb2.ReadRequest(address=address, length=length)
        )
        return self._send(req).read.data

    def write_bytes(self, address: int, data: bytes) -> None:
        """Write raw bytes to target memory."""
        req = microdatawiggler_pb2.MemoryRequest(
            write=microdatawiggler_pb2.WriteRequest(address=address, data=data)
        )
        self._send(req)

    # ------------------------------------------------------------------
    # Recording API
    # ------------------------------------------------------------------

    def start_recording(
        self,
        variables: list[str],
        sample_rate_hz: int = 1000,
        max_samples: int = 0,
    ) -> None:
        """Start recording named variables at the given rate.

        Requires elf_path to have been passed at construction. The recording
        runs on the server until stop_recording() is called.
        """
        resolver = self._require_resolver()
        specs = []
        recording_vars = []
        for name in variables:
            sym = resolver.resolve(name)
            if sym.count != 1:
                raise ValueError(
                    f"'{name}' is an array ({sym.count} elements); recording arrays is not supported"
                )
            specs.append(
                microdatawiggler_pb2.VariableSpec(address=sym.address, length=sym.size)
            )
            recording_vars.append((name, sym.dtype))

        req = microdatawiggler_pb2.MemoryRequest(
            start_record=microdatawiggler_pb2.StartRecordRequest(
                variables=specs,
                sample_rate_hz=sample_rate_hz,
                max_samples=max_samples,
            )
        )
        self._send(req)
        self._recording_vars = recording_vars

    def start_recording_addrs(
        self,
        specs: list[dict],
        sample_rate_hz: int = 1000,
        max_samples: int = 0,
    ) -> None:
        """Start recording by address. No ELF required.

        Each entry in specs must have keys: address, length, dtype, name.
        """
        var_specs = []
        recording_vars = []
        for s in specs:
            var_specs.append(
                microdatawiggler_pb2.VariableSpec(address=s["address"], length=s["length"])
            )
            recording_vars.append((s["name"], s["dtype"]))

        req = microdatawiggler_pb2.MemoryRequest(
            start_record=microdatawiggler_pb2.StartRecordRequest(
                variables=var_specs,
                sample_rate_hz=sample_rate_hz,
                max_samples=max_samples,
            )
        )
        self._send(req)
        self._recording_vars = recording_vars

    def stop_recording(self) -> None:
        """Stop an active recording."""
        req = microdatawiggler_pb2.MemoryRequest(
            stop_record=microdatawiggler_pb2.StopRecordRequest()
        )
        self._send(req)

    def fetch_recording(self) -> list[dict]:
        """Fetch recorded samples after stop_recording().

        Returns a list of dicts, one per sample, with keys:
          - "timestamp_ms": float, milliseconds since recording started
          - one key per variable name with its unpacked value

        Raises a warning if the server hit the max_samples cap.
        """
        req = microdatawiggler_pb2.MemoryRequest(
            fetch_record=microdatawiggler_pb2.FetchRecordRequest()
        )
        response = self._send(req)
        rec = response.record_data

        if rec.capped:
            warnings.warn(
                "Recording hit max_samples limit; results may be incomplete",
                stacklevel=2,
            )

        results = []
        for sample in rec.samples:
            row: dict = {"timestamp_ms": sample.timestamp_us / 1000.0}
            for (name, dtype), raw in zip(self._recording_vars, sample.data):
                row[name] = unpack(dtype, raw)
            results.append(row)
        return results

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------

    def _send(self, request):
        if self._response_iter is None:
            raise RuntimeError("Not connected. Call connect() first.")
        self._request_queue.put(request)
        response = next(self._response_iter)
        if response.HasField("error"):
            raise RuntimeError(response.error.message)
        return response

    def _request_generator(self):
        while True:
            req = self._request_queue.get()
            if req is None:
                return
            yield req
