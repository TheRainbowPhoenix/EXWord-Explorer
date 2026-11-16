#!/usr/bin/env python3
"""Embed the *complete gint-linked EX-word ELF* in one UPDADN2 image.

Memory layout, starting from the file's first byte:
  +0x0000  128-byte CASIO CY10x updater header, entry 0x8c190000
  +0x0080  guarded physical P1 TLB bootstrap (0x8c190000)
  +0x1070  EXFS magic, payload length, FNV-1a hash and flags
  +0x1080  128-byte gap aligning payload to +0x80 in a 4-KiB page
  +0x1100  full gint EXWord Explorer .text and loadable .data,
           linked for 0x70000080, physically loaded at 0x8c191080

This image compiles and validates structurally only. No EX-word E model
hardware has been tested for the OS Update menu or its service tables.
"""
from pathlib import Path
import argparse
import struct
import subprocess
import tempfile

HEADER = b"CASIO CY10x ADDIN MODULE FOR RELEASE ROM\0"
STUB_SIZE = 0x1080
PAYLOAD_OFFSET = 0x1100
HEADER_PHYS = 0x8C18FF80
ENTRY_PHYS = 0x8C190000
APP_ENTRY = 0x70000080
META_OFFSET = 0x1070

def fnv1a(data):
    value=2166136261
    for byte in data:
        value=((value ^ byte)*16777619)&0xffffffff
    return value

def generate(stub_path,elf_path,output_path,objcopy="sh-elf-objcopy",
             readelf="sh-elf-readelf"):
    stub=stub_path.read_bytes()
    if len(stub)!=STUB_SIZE or not stub.startswith(HEADER):
        raise ValueError(f"Malformed bootstrap header/length: {len(stub)}")
    if struct.unpack_from(">II",stub,48)!=(HEADER_PHYS,ENTRY_PHYS):
        raise ValueError("Unexpected bootstrap entry physical address")
    header=subprocess.check_output([readelf,"-h",str(elf_path)],text=True)
    if f"0x{APP_ENTRY:x}" not in header:
        raise ValueError("Application must keep original gint entry at 0x70000080")
    with tempfile.TemporaryDirectory() as folder:
        binary=Path(folder)/"app.bin"
        subprocess.run([objcopy,"-O","binary",str(elf_path),str(binary)],
                       check=True)
        image=binary.read_bytes()
    if not 1024<len(image)<=128*1024:
        raise ValueError(f"Full Explorer image too large/small: {len(image)}")
    if all(not b for b in image[:256]):
        raise ValueError("Empty application entry bytes")

    meta=struct.pack(">IIII",0x45584653,len(image),fnv1a(image),1)
    stub=stub[:META_OFFSET]+meta+stub[META_OFFSET+16:]
    assert len(stub)==STUB_SIZE
    full=stub+bytes(PAYLOAD_OFFSET-STUB_SIZE)+image
    output_path.parent.mkdir(parents=True,exist_ok=True)
    output_path.write_bytes(full)

    # Check exactly what will be served to the updater, not just its ELF.
    reread=output_path.read_bytes()
    assert reread[:len(HEADER)]==HEADER
    assert struct.unpack_from(">II",reread,48)==(HEADER_PHYS,ENTRY_PHYS)
    assert struct.unpack_from(">IIII",reread,META_OFFSET)==(
        0x45584653,len(image),fnv1a(image),1)
    assert reread[PAYLOAD_OFFSET:]==image
    print(f"UPDADN2-FULL image: {len(full)} bytes, full Explorer "
          f"ROM+data {len(image)} bytes, {((len(image)+128+4095)//4096)} TLB pages")
    return full

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--stub",type=Path,required=True)
    parser.add_argument("--elf",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--objcopy",default="sh-elf-objcopy")
    parser.add_argument("--readelf",default="sh-elf-readelf")
    args=parser.parse_args()
    generate(args.stub,args.elf,args.out,args.objcopy,args.readelf)

if __name__=="__main__":
    main()
