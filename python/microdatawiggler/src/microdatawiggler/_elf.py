import struct
from dataclasses import dataclass

from elftools.elf.elffile import ELFFile


@dataclass(frozen=True)
class Symbol:
    name: str
    type: str    # human-readable, e.g. "volatile int8_t" or "volatile uint8_t [8]"
    dtype: str   # pack/unpack key, e.g. "int8", "uint8"
    address: int
    size: int    # total bytes
    count: int   # 1 for scalars, N for arrays


_TYPEDEF_TO_DTYPE: dict[str, str] = {
    "int8_t":   "int8",    "uint8_t":  "uint8",
    "int16_t":  "int16",   "uint16_t": "uint16",
    "int32_t":  "int32",   "uint32_t": "uint32",
    "int64_t":  "int64",   "uint64_t": "uint64",
    "bool":     "bool",    "_Bool":    "bool",
}

_BASE_TO_DTYPE: dict[str, str] = {
    "float":                  "float",
    "double":                 "double",
    "_Bool":                  "bool",
    "signed char":            "int8",
    "unsigned char":          "uint8",
    "short":                  "int16",
    "short int":              "int16",
    "unsigned short":         "uint16",
    "unsigned short int":     "uint16",
    "int":                    "int32",
    "unsigned int":           "uint32",
    "long int":               "int32",
    "unsigned long int":      "uint32",
    "long long int":          "int64",
    "unsigned long long int": "uint64",
    "long long":              "int64",
    "unsigned long long":     "uint64",
}


class ElfSymbolResolver:
    """Resolves global variable symbols from an ELF file using DWARF debug info."""

    def __init__(self, elf_path: str):
        self._symbols: dict[str, Symbol] = {}
        with open(elf_path, "rb") as f:
            elf = ELFFile(f)
            if not elf.has_dwarf_info():
                raise RuntimeError(f"{elf_path} has no DWARF debug info")
            dwarf = elf.get_dwarf_info()
            for cu in dwarf.iter_CUs():
                self._parse_cu(cu.get_top_DIE(), cu["address_size"])

    def resolve(self, name: str) -> Symbol:
        """Return the Symbol for a global variable name, or raise RuntimeError."""
        if name not in self._symbols:
            raise RuntimeError(f"Symbol not found: {name!r}")
        return self._symbols[name]

    def _parse_cu(self, top_die, addr_size: int) -> None:
        for die in top_die.iter_children():
            if die.tag == "DW_TAG_variable" and "DW_AT_name" in die.attributes:
                sym = self._make_symbol(die, addr_size)
                if sym is not None:
                    self._symbols[sym.name] = sym

    def _make_symbol(self, die, addr_size: int) -> Symbol | None:
        name = die.attributes["DW_AT_name"].value.decode()

        if "DW_AT_location" not in die.attributes or "DW_AT_type" not in die.attributes:
            return None

        address = self._parse_location(die.attributes["DW_AT_location"], addr_size)
        if address is None:
            return None

        try:
            type_die = die.get_DIE_from_attribute("DW_AT_type")
            info = self._resolve_type(type_die, [])
        except Exception:
            return None

        qualifiers = " ".join(info["qualifiers"])
        type_str = f"{qualifiers} {info['type_name']}".strip() if qualifiers else info["type_name"]

        if not info["dtype"] or info["dtype"] == "raw":
            return None

        return Symbol(
            name=name,
            type=type_str,
            dtype=info["dtype"],
            address=address,
            size=info["size"],
            count=info["count"],
        )

    def _parse_location(self, loc_attr, addr_size: int) -> int | None:
        if loc_attr.form not in (
            "DW_FORM_exprloc",
            "DW_FORM_block",
            "DW_FORM_block1",
            "DW_FORM_block2",
            "DW_FORM_block4",
        ):
            return None  # location list — skip
        expr = bytes(loc_attr.value)
        if not expr or expr[0] != 0x03:  # DW_OP_addr
            return None
        addr_bytes = expr[1: 1 + addr_size]
        if len(addr_bytes) != addr_size:
            return None
        fmt = "<I" if addr_size == 4 else "<Q"
        return struct.unpack(fmt, addr_bytes)[0]

    def _resolve_type(self, die, qualifiers: list[str]) -> dict:
        tag = die.tag

        if tag == "DW_TAG_volatile_type":
            child = die.get_DIE_from_attribute("DW_AT_type")
            return self._resolve_type(child, qualifiers + ["volatile"])

        if tag == "DW_TAG_const_type":
            child = die.get_DIE_from_attribute("DW_AT_type")
            return self._resolve_type(child, qualifiers + ["const"])

        if tag == "DW_TAG_typedef":
            name = die.attributes["DW_AT_name"].value.decode()
            child = die.get_DIE_from_attribute("DW_AT_type")
            result = self._resolve_type(child, [])
            dtype = _TYPEDEF_TO_DTYPE.get(name, result["dtype"])
            return {**result, "qualifiers": qualifiers, "type_name": name, "dtype": dtype}

        if tag == "DW_TAG_base_type":
            attr = die.attributes.get("DW_AT_name")
            name = attr.value.decode() if attr else "unknown"
            size = die.attributes["DW_AT_byte_size"].value
            return {
                "qualifiers": qualifiers,
                "type_name": name,
                "dtype": _BASE_TO_DTYPE.get(name, "raw"),
                "size": size,
                "count": 1,
            }

        if tag == "DW_TAG_array_type":
            elem_die = die.get_DIE_from_attribute("DW_AT_type")
            elem = self._resolve_type(elem_die, [])

            count = 1
            for child in die.iter_children():
                if child.tag == "DW_TAG_subrange_type":
                    if "DW_AT_count" in child.attributes:
                        count = child.attributes["DW_AT_count"].value
                    elif "DW_AT_upper_bound" in child.attributes:
                        count = child.attributes["DW_AT_upper_bound"].value + 1
                    break

            if "DW_AT_byte_size" in die.attributes:
                size = die.attributes["DW_AT_byte_size"].value
            else:
                size = elem["size"] * count

            return {
                "qualifiers": qualifiers,
                "type_name": f"{elem['type_name']} [{count}]",
                "dtype": elem["dtype"],
                "size": size,
                "count": count,
            }

        return {"qualifiers": qualifiers, "type_name": "unknown", "dtype": "raw", "size": 0, "count": 1}
