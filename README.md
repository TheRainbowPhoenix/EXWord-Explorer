# EXWord Explorer

A full-screen 528x320 EX-word file explorer and hex viewer based on [gint-exword](https://github.com/TheRainbowPhoenix/gint-exword).

This is an **EX-word .d01 add-in**, not a ClassPad .hh3 add-in.

## Features

- Browse candidate volumes drv0, sys0, data0, data1, data2, crd0, crd1, crd2. Directory enumeration returning -1 is shown as an empty directory; other native errors are reported.
- Directory browsing includes all enumerated filenames, with no extension or hidden-file filtering.
- Full-screen list, fixed 20-row window; the app does not cache a whole directory.
- Hex viewer: 12 bytes per row with address, hex and ASCII; 240-byte pages let you browse huge files with little RAM.
- Delete, rename, create file, create folder, copy, cut and paste using native EX-word filesystem calls.
- Only one file in the clipboard, no recursive directory copying. Copy/cut avoids overwriting, and cut removes the original only if the copy succeeds.
- Select a byte in the hex viewer, press E, enter two hex digits, press EXE twice to save (with confirmation).

## Key bindings

| Key | Browser | Hex view |
|---|---|---|
| EXE | Open folder or view file | Confirm edit |
| ESC | Back one directory; exit at root | Return to browser; cancel edit |
| AC/ON | Exit app | Exit app |
| Up/Down | Move through list | Move 12 bytes |
| Page Up/Down | Move 20 entries | Move 240 bytes |
| Sound/Audio | File operations menu | No operation |
| Left/Right | No operation | Previous/next byte |
| E | No operation | Edit selected byte |

Touch a file once to select; touch again to enter it. In a filename
dialog, use alphabet keys to type letters, SHIFT for capitals,
**ALT+A for a dot (.)**, and **ALT+Q..P for digits 1..0**.
DEL erases a character; EXE confirms and ESC cancels.

The context menu includes Delete, Rename, Create file, Create folder,
Copy, Cut, Paste. Delete and editing require explicit confirmation.
No operation overwrites an existing filename.

## Limits / safety

Back up files before attempting writes, especially on sys0 or rom0.
The program does not bypass firmware permissions. Read-only, missing or
protected mounts/files may not be accessible. A volume returning the same
error for an empty folder and an inaccessible device can appear empty.

Files whose filenames have non-ASCII bytes are still included, but their
labels use question marks for unsupported glyphs. File operations preserve
their actual bytes. Names exceeding the 159-byte limit are marked LONG and
are not operated on to avoid path truncation. Paths are limited to 319 bytes.
Offsets use the native signed 32-bit file API. Copy/cut only handles files.

CI compilation and package generation does not prove on-device behavior.

## Build / install

The GitHub Actions workflow builds using:
- TheRainbowPhoenix/fxsdk-exword, fix/exword-data-load-alignment
- TheRainbowPhoenix/gint-exword, feature/exword-runtime-bridge
- brijohn/elf2d01
- TheRainbowPhoenix/exword-xd-a8500-TEST HTML templates

Use the EXWord-Explorer-ja or EXWord-Explorer-cn artifacts from a successful
Actions run. Each contains an EXPLR installation folder for the EX-word.
Install with libexword using a valid existing authentication setup. Do not
reset installed dictionaries for a test installation.

The workflow ensures that explorer.d01 fits the 128 KiB EX-word loader limit.


## USB remote filesystem (fxlink + local TFTP)

The volume home screen lists all eight registered drives (**drv0, sys0, data0, data1, data2, crd0, crd1, crd2**), plus **USB Remote**. This opens a USB
fxlink bulk interface and runs the EXFS/1 filesystem RPC server. The
companion desktop app (Python 3.10+, libusb and Tkinter) provides a
graphical file manager, uploads/downloads and a **localhost-only TFTP
bridge** usable with standard TFTP clients in binary/octet mode.

GitHub Actions also builds **libfxlink.so** and uploads the
**EXWord-Explorer-host-linux** artifact alongside Japanese and Chinese
EX-word installation packages. Its scripts are in host/exfs.py and
host/bridge.py; detailed commands and the protocol are in
[host/README.md](host/README.md).

The TFTP listener binds to 127.0.0.1:6969 and is read-only by default.
Explicit --allow-write enables TFTP uploading new files. The desktop GUI
offers file send, receive, rename, delete and create-folder actions.
A virtual .exword-list text file provides listings to command-line TFTP
clients. No overwriting is implicit.

The directory and hex screens now show their full path in a **single
header row**, with compact human-readable byte size at the far right of
the hex viewer. Folder loads display "Loading..." while the native
enumeration runs, moving between known directory pages stops scanning
once the window is filled, and going back from a child directory restores
the previous selection and scroll offset.

**Hardware testing:** The USB service compiles and the Python bridge
passes offline binary TFTP tests in CI. This does not establish that the
EX-word USB peripheral can coexist with OS filesystem syscalls in
practice; please verify on a device, and back up files before writes.

### USB receiver fix and diagnostics

The Python host no longer reopens bulk-IN after receiving a packet.
The underlying libfxlink callback already resubmits the receiver, so
trying to start a second receive transfer incorrectly raises a
`Could not restart fxlink receiver` exception after a successful ping.
Use `python3 bridge.py --verbose` for USB connection, EXFS request ID,
reply code and timeout diagnostics (stderr). Re-download the latest
host artifact or update both `bridge.py` and `exfs.py` before retrying.

## Image and CJF preview

Select a file and press **EXE** to open the hex viewer. Press **SOUND**
in the **hex viewer** to open its options menu:

- **Preview as bitmap:** safely decode Casio packed BIN image variants
  0/1/2/3, limited to at most 1 MiB input, 1024x768 dimensions,
  512 KiB pixel payload and a bounded row buffer
- **Preview as font:** offered only for a file whose first 3 bytes are
  `43 4A 46` (`CJF`)
- **Factory RLE565 (528x320):** a separate option for headerless factory
  images no larger than 512 KiB

These previews are no longer in the directory context menu.
**ESC** from an image or CJF grid returns to the same file and offset
in the hex viewer. CJF uses an unlabeled-borderless grid with glyph
indices below each glyph. **PgUp/PgDn** changes glyph pages and
**Left/Right** switches font ranges.

The `src/readers/casio_bitmap.{c,h}` and `src/readers/cjf.{c,h}`
decoders are independent of gint and native EX-word APIs, suitable for
future upstreaming. `src/preview.c` bridges their read-at callbacks to
the device's filesystem and screen. Bitmap formats are **Casio packed
BIN**, not standard Windows BMP.

The supplied `sb24f.cjf` uses **encoding 9** and nine ranges with
**256 physically stored glyph slots** each. The reader now enumerates
physical slots from range data offsets rather than relying on a known
character-code mapping. The preview begins at the first nonblank
glyph so leading empty slots do not make the display appear broken.
A synthetic regression test reproduces this layout without bundling
the sample's font artwork.

## Transfer improvements / stop controls

TFTP itself uses **512-byte DATA blocks** (RFC 1350) and remains
compatible with ordinary TFTP clients. USB EXFS requests now batch up
to **2048 bytes**, with the host buffering multiple TFTP blocks from
one USB request. Host downloads and uploads stream to disk instead of
buffering whole files. The desktop GUI shows byte progress and has a
**Cancel** button; cancellation takes effect after the in-flight USB
chunk finishes and incomplete uploads are cleaned up where possible.
On-device USB Remote shows a tappable **STOP USB** control; ESC,
AC/ON, and the Sound/Audio key can also disconnect. A bounded USB
read timeout prevents permanently blocking on an incomplete message.

The Windows fxlink DLL is now built without terminal ANSI escape sequences.
Actual transfer speed and reliable stop/disconnect behavior still require
on-device tests; 2048 bytes is a design compromise for limited EX-word RAM,
not a measured optimum.

## USB Remote display

The landscape USB Remote screen shows a scannable QR code for
**http://pho3.be/go/exxplorer**, the companion-app address, the local
PC TFTP endpoint **127.0.0.1:6969**, connected status, request count,
current transfer's native path, and progress as chunks completed/total.
The host sends a lightweight EXFS `BEGIN` announcement (command 9)
with the file size so the device can display accurate upload progress;
plain TFTP uploads without a known size show `Chunk N / ?` instead.
The existing **STOP USB** touch button and ESC/AC-ON/SOUND shortcuts
remain available. Both the updated device package and host scripts
are required for transfer progress announcements.
