import queue

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
    """

    def __init__(self, address: str = "localhost:50051", elf_path: str | None = None):
        self._address = address
        self._channel = None
        self._request_queue = None
        self._response_iter = None
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
