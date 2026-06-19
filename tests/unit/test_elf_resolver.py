"""
Tests for ElfSymbolResolver using the pre-built firmware ELF.

The ELF at firmware_build/simple.elf is built from validation_firmware/simple/firmware.c
and contains volatile-qualified variables of every primitive type plus an array.
"""
import os
import pytest

from microdatawiggler._elf import ElfSymbolResolver, Symbol

ELF_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "firmware_build", "simple.elf")


@pytest.fixture(scope="module")
def resolver():
    if not os.path.exists(ELF_PATH):
        pytest.skip("firmware_build/simple.elf not found — run ./run.bash build-validation-firmware first")
    return ElfSymbolResolver(ELF_PATH)


# ---------------------------------------------------------------------------
# Unknown symbol
# ---------------------------------------------------------------------------

def test_resolve_unknown_raises(resolver):
    with pytest.raises(RuntimeError, match="Symbol not found"):
        resolver.resolve("does_not_exist")


# ---------------------------------------------------------------------------
# Return type
# ---------------------------------------------------------------------------

def test_resolve_returns_symbol(resolver):
    sym = resolver.resolve("in_int8_t")
    assert isinstance(sym, Symbol)


# ---------------------------------------------------------------------------
# Sizes
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("name,expected_size", [
    ("in_int8_t",   1),
    ("in_uint8_t",  1),
    ("in_bool",     1),
    ("in_int16_t",  2),
    ("in_uint16_t", 2),
    ("in_int32_t",  4),
    ("in_uint32_t", 4),
    ("in_float",    4),
    ("in_int64_t",  8),
    ("in_uint64_t", 8),
    ("in_double",   8),
])
def test_variable_size(resolver, name, expected_size):
    assert resolver.resolve(name).size == expected_size


# ---------------------------------------------------------------------------
# dtypes
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("name,expected_dtype", [
    ("in_int8_t",   "int8"),
    ("in_int16_t",  "int16"),
    ("in_int32_t",  "int32"),
    ("in_int64_t",  "int64"),
    ("in_uint8_t",  "uint8"),
    ("in_uint16_t", "uint16"),
    ("in_uint32_t", "uint32"),
    ("in_uint64_t", "uint64"),
    ("in_float",    "float"),
    ("in_double",   "double"),
    ("in_bool",     "bool"),
])
def test_variable_dtype(resolver, name, expected_dtype):
    assert resolver.resolve(name).dtype == expected_dtype


# ---------------------------------------------------------------------------
# Type strings contain the expected type name (volatile-qualified)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("name,expected_fragment", [
    ("in_int8_t",   "int8_t"),
    ("in_uint32_t", "uint32_t"),
    ("in_float",    "float"),
    ("in_double",   "double"),
    ("in_bool",     "_Bool"),  # ARM GCC emits _Bool in DWARF; bool is a macro
])
def test_type_string_contains_type_name(resolver, name, expected_fragment):
    assert expected_fragment in resolver.resolve(name).type


def test_volatile_qualifier_in_type_string(resolver):
    assert "volatile" in resolver.resolve("in_int8_t").type


# ---------------------------------------------------------------------------
# Addresses
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("name", ["in_int8_t", "in_uint32_t", "in_float"])
def test_address_nonzero(resolver, name):
    assert resolver.resolve(name).address > 0


def test_addresses_are_distinct(resolver):
    names = ["in_int8_t", "in_int16_t", "in_int32_t", "in_float", "in_double"]
    addrs = [resolver.resolve(n).address for n in names]
    assert len(set(addrs)) == len(addrs)


# ---------------------------------------------------------------------------
# Scalar count
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("name", ["in_int8_t", "in_float", "in_double"])
def test_scalar_count_is_one(resolver, name):
    assert resolver.resolve(name).count == 1


# ---------------------------------------------------------------------------
# Array
# ---------------------------------------------------------------------------

def test_array_count(resolver):
    assert resolver.resolve("in_arr").count == 8


def test_array_size(resolver):
    assert resolver.resolve("in_arr").size == 8


def test_array_dtype(resolver):
    assert resolver.resolve("in_arr").dtype == "uint8"


def test_array_type_string(resolver):
    sym = resolver.resolve("in_arr")
    assert "uint8_t" in sym.type
    assert "[8]" in sym.type


# ---------------------------------------------------------------------------
# Missing DWARF raises
# ---------------------------------------------------------------------------

def test_missing_dwarf_raises(tmp_path):
    import subprocess
    src = tmp_path / "empty.c"
    elf = tmp_path / "empty.elf"
    src.write_text("int main(void) { return 0; }\n")
    subprocess.run(
        ["arm-none-eabi-gcc", "-nostdlib", str(src), "-o", str(elf)],
        check=True, capture_output=True,
    )
    with pytest.raises(RuntimeError, match="no DWARF debug info"):
        ElfSymbolResolver(str(elf))
