import struct

_FORMATS: dict[str, tuple[str, int]] = {
    "int8":   ("<b", 1),
    "uint8":  ("<B", 1),
    "int16":  ("<h", 2),
    "uint16": ("<H", 2),
    "int32":  ("<i", 4),
    "uint32": ("<I", 4),
    "int64":  ("<q", 8),
    "uint64": ("<Q", 8),
    "float":  ("<f", 4),
    "double": ("<d", 8),
    "bool":   ("<B", 1),
}


def dtype_size(dtype: str) -> int:
    try:
        return _FORMATS[dtype][1]
    except KeyError:
        raise ValueError(f"Unknown dtype '{dtype}'. Valid: {sorted(_FORMATS)}")


def pack(dtype: str, value) -> bytes:
    try:
        fmt, _ = _FORMATS[dtype]
    except KeyError:
        raise ValueError(f"Unknown dtype '{dtype}'. Valid: {sorted(_FORMATS)}")
    if dtype == "bool":
        value = 1 if value else 0
    return struct.pack(fmt, value)


def unpack(dtype: str, data: bytes):
    try:
        fmt, size = _FORMATS[dtype]
    except KeyError:
        raise ValueError(f"Unknown dtype '{dtype}'. Valid: {sorted(_FORMATS)}")
    if len(data) != size:
        raise ValueError(f"Expected {size} bytes for '{dtype}', got {len(data)}")
    (value,) = struct.unpack(fmt, data)
    if dtype == "bool":
        return bool(value)
    return value
