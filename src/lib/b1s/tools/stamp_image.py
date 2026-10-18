#!/usr/bin/env python3
"""Stamps size and CRC into the image header of a B1S firmware (FIRMWARE.md).

Reads the ELF file, builds the flash image (objcopy -O binary), pads it to a
multiple of 8 bytes, fills in the header's image size and CRC-32 (zlib, with
the four CRC bytes counted as 0xFF), and writes the header back into the ELF
file (objcopy --update-section) and the binary. Images flashed with the
ST-Link and images sent over CAN are therefore the same.

    stamp_image.py --objcopy arm-none-eabi-objcopy firmware.elf firmware.bin
"""

import argparse
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

HEADER_OFFSET = 0x200
HEADER_SIZE = 64
MAGIC = 0x21533142
CRC_FIELD = HEADER_OFFSET + 16
MAX_SIZE = 224 * 1024


def stamp(image):
    """Returns the stamped image (bytes), padded to a multiple of 8."""
    image = bytearray(image)
    image += b"\xff" * (-len(image) % 8)
    if len(image) < HEADER_OFFSET + HEADER_SIZE:
        sys.exit("image too short for a header")
    magic, version = struct.unpack_from("<IH", image, HEADER_OFFSET)
    if magic != MAGIC or version != 1:
        sys.exit("no B1S image header at offset 0x200 (can_mcu::image::Header in section .image_header)")
    if len(image) > MAX_SIZE:
        sys.exit(f"image of {len(image)} bytes exceeds the {MAX_SIZE}-byte application slot")
    struct.pack_into("<I", image, HEADER_OFFSET + 12, len(image))
    image[CRC_FIELD:CRC_FIELD + 4] = b"\xff\xff\xff\xff"
    struct.pack_into("<I", image, CRC_FIELD, zlib.crc32(image))
    return bytes(image)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--objcopy", default="arm-none-eabi-objcopy")
    parser.add_argument("elf", type=pathlib.Path)
    parser.add_argument("bin", type=pathlib.Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        raw = pathlib.Path(directory) / "raw.bin"
        subprocess.run([args.objcopy, "-O", "binary", str(args.elf), str(raw)], check=True)
        image = stamp(raw.read_bytes())
        header = pathlib.Path(directory) / "header.bin"
        header.write_bytes(image[HEADER_OFFSET:HEADER_OFFSET + HEADER_SIZE])
        subprocess.run([args.objcopy, "--update-section", f".image_header={header}", str(args.elf)], check=True)
    args.bin.write_bytes(image)
    size, crc = struct.unpack_from("<II", image, HEADER_OFFSET + 12)
    text = image[HEADER_OFFSET + 24:HEADER_OFFSET + 64].split(b"\0")[0].decode("ascii", "replace")
    print(f"stamp_image: {args.bin.name}: {size} bytes, CRC {crc:08X}, {text}")


if __name__ == "__main__":
    main()
