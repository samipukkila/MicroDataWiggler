#!/usr/bin/env python3
import time

from microdatawiggler import MicrodatawigglerClient, MicrodatawigglerLauncher

ELF_PATH = "/elf_files/simple.elf"


def main():
    with MicrodatawigglerLauncher():
        with MicrodatawigglerClient("localhost:50051", elf_path=ELF_PATH) as client:
            print("\n--- Monitoring cnt_uint8_t ---")
            start = time.monotonic()
            while time.monotonic() < start + 0.5:
                value = client.read_variable("cnt_uint8_t")
                print(f"  cnt_uint8_t = {value}")
                time.sleep(0.05)

            # --- Read-Write-Read test ---
            print("\n--- Read-Write-Read test ---")
            test_value1 = 0xAAAA
            test_value2 = 0xABCD

            client.write_variable("in_uint32_t", test_value1)
            time.sleep(0.005)  # firmware loop copies the value every ~1 ms
            readback1 = client.read_variable("out_uint32_t")
            print(f"  test_value1 = {hex(test_value1)}, readback1 = {hex(readback1)}")
            if test_value1 != readback1:
                raise RuntimeError(f"test_value1 ({hex(test_value1)}) != readback1 ({hex(readback1)})")

            client.write_variable("in_uint32_t", test_value2)
            time.sleep(0.005)  # firmware loop copies the value every ~1 ms
            readback2 = client.read_variable("out_uint32_t")
            print(f"  test_value2 = {hex(test_value2)}, readback2 = {hex(readback2)}")
            if test_value2 != readback2:
                raise RuntimeError(f"test_value2 ({hex(test_value2)}) != readback2 ({hex(readback2)})")


if __name__ == "__main__":
    main()
