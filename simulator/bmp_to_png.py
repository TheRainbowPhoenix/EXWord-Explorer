#!/usr/bin/env python3
"""Convert SDL2 RGB565 screenshots to PNG without third-party Python modules."""
from pathlib import Path
import struct
import sys
import zlib

def png_chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag+data) & 0xffffffff))

def convert(src: Path, dst: Path):
    raw = src.read_bytes()
    if raw[:2] != b"BM":
        raise ValueError(f"{src}: expected SDL BMP screenshot")
    offset = struct.unpack_from("<I", raw, 10)[0]
    width, height = struct.unpack_from("<ii", raw, 18)
    planes, bpp = struct.unpack_from("<HH", raw, 26)
    compression = struct.unpack_from("<I", raw, 30)[0]
    if (width, abs(height), planes, bpp) != (528, 320, 1, 16) or compression not in (0,3):
        raise ValueError(f"{src}: unexpected EX-word pixel layout")
    pitch = (width*2+3)&~3
    pixels = bytearray()
    for y in range(abs(height)):
        origin = offset + (abs(height)-1-y if height>0 else y)*pitch
        pixels.append(0)  # PNG filter 0
        for x in range(width):
            value = struct.unpack_from("<H",raw,origin+2*x)[0]
            r=(value>>11)&31
            g=(value>>5)&63
            b=value&31
            pixels.extend(((r*255+15)//31,(g*255+31)//63,(b*255+15)//31))
    compressed=zlib.compress(bytes(pixels),9)
    signature=b"\x89PNG\r\n\x1a\n"
    ihdr=struct.pack(">IIBBBBB",width,abs(height),8,2,0,0,0)
    dst.write_bytes(signature+png_chunk(b"IHDR",ihdr)+
                    png_chunk(b"IDAT",compressed)+png_chunk(b"IEND",b""))

def main():
    folder=Path(sys.argv[1] if len(sys.argv)>1 else "build-sim/screenshots")
    for src in sorted(folder.glob("*.bmp")):
        target=src.with_suffix(".png")
        convert(src,target)
        data=target.read_bytes()
        if len(set(data))<16:
            raise AssertionError(f"{target}: unexpected near-empty screenshot")
        print(f"PNG {target.name}: {len(data)} bytes")
    names={p.stem for p in folder.glob("*.png")}
    required={"home","directory","hex-viewer","hex-options",
              "font-grid","bitmap","usb-remote"}
    missing=required-names
    if missing:
        raise AssertionError(f"Missing screenshots: {sorted(missing)}")
    print("Seven real-UI mock screenshots validated")

if __name__=="__main__":
    main()
