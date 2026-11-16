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
    if (width, abs(height), planes) != (528, 320, 1) or (
            bpp not in (16, 24, 32) or compression not in (0, 3)):
        raise ValueError(f"{src}: unexpected EX-word pixel layout: "
                         f"{width}x{height} planes={planes} bpp={bpp} compression={compression}")
    pitch = (width*(bpp//8)+3)&~3
    pixels = bytearray()
    for y in range(abs(height)):
        origin = offset + (abs(height)-1-y if height>0 else y)*pitch
        pixels.append(0)  # PNG filter 0
        for x in range(width):
            if bpp == 16:
                value = struct.unpack_from("<H",raw,origin+2*x)[0]
                r=(value>>11)&31
                g=(value>>5)&63
                b=value&31
                pixels.extend(((r*255+15)//31,(g*255+31)//63,(b*255+15)//31))
            else:
                step=bpp//8
                b,g,r=raw[origin+x*step:origin+x*step+3]
                pixels.extend((r,g,b))
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
    # Inspect rendered pixels as well as file headers. This catches a blank
    # or misplaced screen even when the SDL program exits with status 0.
    for name in sorted(required):
        data=(folder/f"{name}.png").read_bytes()
        p=8
        chunks=[]
        while p<len(data):
            n=struct.unpack_from(">I",data,p)[0]
            tag=data[p+4:p+8]
            if tag==b"IDAT":
                chunks.append(data[p+8:p+8+n])
            p+=12+n
        rgb=zlib.decompress(b"".join(chunks))
        stride=528*3+1
        assert len(rgb)==320*stride
        assert all(rgb[y*stride]==0 for y in range(320))
        colors=set()
        nonwhite=0
        for y in range(320):
            start=y*stride+1
            row=rgb[start:start+528*3]
            for x in range(0,len(row),3):
                c=bytes(row[x:x+3])
                colors.add(c)
                nonwhite+=c!=b"\xff\xff\xff"
        assert len(colors)>=3 and nonwhite>1200, (
            f"{name}: blank or nearly blank mock page")
        if name=="bitmap":
            assert len(colors)>1000, "Bitmap preview did not render its colors"
        if name=="font-grid":
            assert b"\x00\x00\x00" in colors, "CJF preview lacks glyphs"
        if name=="usb-remote":
            black=0
            for y in range(81,168):
                for x in range(41,128):
                    pixel=rgb[y*stride+1+3*x:y*stride+1+3*x+3]
                    if pixel==b"\x00\x00\x00":
                        black+=1
            assert black>1500, "The USB QR code is missing"
    print("Seven simulated pages have nonempty real pixel content")

if __name__=="__main__":
    main()
