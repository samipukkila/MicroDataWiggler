import struct
import pytest
from microdatawiggler._types import pack, unpack, dtype_size


@pytest.mark.parametrize("dtype,size", [
    ("int8", 1), ("uint8", 1),
    ("int16", 2), ("uint16", 2),
    ("int32", 4), ("uint32", 4),
    ("int64", 8), ("uint64", 8),
    ("float", 4), ("double", 8),
    ("bool", 1),
])
def test_dtype_size(dtype, size):
    assert dtype_size(dtype) == size


def test_dtype_size_unknown_raises():
    with pytest.raises(ValueError):
        dtype_size("unknown")


@pytest.mark.parametrize("dtype,value", [
    ("int8",   0),
    ("int8",   127),
    ("int8",  -128),
    ("uint8",  0),
    ("uint8",  255),
    ("int16",  0),
    ("int16",  32767),
    ("int16", -32768),
    ("uint16", 0),
    ("uint16", 65535),
    ("int32",  0),
    ("int32",  2147483647),
    ("int32", -2147483648),
    ("uint32", 0),
    ("uint32", 4294967295),
    ("int64",  0),
    ("int64",  2**63 - 1),
    ("int64", -(2**63)),
    ("uint64", 0),
    ("uint64", 2**64 - 1),
    ("float",  0.0),
    ("float",  3.14),
    ("float", -3.14),
    ("double", 0.0),
    ("double", 3.14),
    ("double", -1.5e100),
    ("bool",   True),
    ("bool",   False),
])
def test_pack_unpack_roundtrip(dtype, value):
    data = pack(dtype, value)
    assert len(data) == dtype_size(dtype)
    result = unpack(dtype, data)
    if dtype in ("float", "double"):
        assert result == pytest.approx(value)
    else:
        assert result == value


def test_pack_bool_truthy():
    assert unpack("bool", pack("bool", 1)) is True
    assert unpack("bool", pack("bool", 0)) is False


def test_pack_unknown_dtype_raises():
    with pytest.raises(ValueError):
        pack("int128", 0)


def test_unpack_unknown_dtype_raises():
    with pytest.raises(ValueError):
        unpack("int128", b"\x00")


def test_unpack_wrong_length_raises():
    with pytest.raises(ValueError):
        unpack("int32", b"\x00\x00")


def test_pack_int32_little_endian():
    assert pack("int32", 1) == b"\x01\x00\x00\x00"


def test_pack_uint16_little_endian():
    assert pack("uint16", 0x0102) == b"\x02\x01"
