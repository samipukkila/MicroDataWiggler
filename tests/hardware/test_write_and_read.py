import time

import pytest


def _write_read(client, write_var, read_var, value):
    client.write_variable(write_var, value)
    time.sleep(0.002)
    return client.read_variable(read_var)


def test_write_nonexistent_fails(client):
    with pytest.raises(Exception):
        client.write_variable("non_existant_var", 100)


def test_read_nonexistent_fails(client):
    with pytest.raises(Exception):
        client.read_variable("non_existant_var")


@pytest.mark.parametrize("write_var,read_var,value", [
    ("in_int8_t",   "out_int8_t",   100),
    ("in_int8_t",   "out_int8_t",  -100),
    ("in_int8_t",   "out_int8_t",   0),
    ("in_int16_t",  "out_int16_t",  100),
    ("in_int16_t",  "out_int16_t", -100),
    ("in_int16_t",  "out_int16_t",  0),
    ("in_int32_t",  "out_int32_t",  100),
    ("in_int32_t",  "out_int32_t", -100),
    ("in_int32_t",  "out_int32_t",  0),
    ("in_int64_t",  "out_int64_t",  100),
    ("in_int64_t",  "out_int64_t", -100),
    ("in_int64_t",  "out_int64_t",  0),
    ("in_uint16_t", "out_uint16_t", 100),
    ("in_uint16_t", "out_uint16_t", 0),
    ("in_uint32_t", "out_uint32_t", 100),
    ("in_uint32_t", "out_uint32_t", 0),
    ("in_uint64_t", "out_uint64_t", 100),
    ("in_uint64_t", "out_uint64_t", 0),
])
def test_write_and_read_integer(client, write_var, read_var, value):
    assert _write_read(client, write_var, read_var, value) == value


@pytest.mark.parametrize("write_var,read_var,value", [
    ("in_float",  "out_float",  -3.14),
    ("in_float",  "out_float",   0),
    ("in_float",  "out_float",   3.14),
    ("in_double", "out_double", -3.14),
    ("in_double", "out_double",  0),
    ("in_double", "out_double",  3.14),
])
def test_write_and_read_float(client, write_var, read_var, value):
    assert _write_read(client, write_var, read_var, value) == pytest.approx(value)
