# EXWord Explorer

A full-screen 528x320 EX-word file explorer and hex viewer based on [gint-exword](https://github.com/TheRainbowPhoenix/gint-exword).

This is an **EX-word .d01 add-in**, not a ClassPad .hh3 add-in.

## Features

- Browse candidate volumes drv0, drv1, drv2, crd0, crd1, crd2, sys0, sys1, rom0, rom1. Unsupported mounts are safely rejected, or shown as empty if the OS reports the same error as an empty directory.
- Directory browsing includes all enumerated filenames, with no extension or hidden-file filtering.
- Full-screen list, fixed 18-row window; the app does not cache a whole directory.
- Hex viewer: 12 bytes per row with address, hex and ASCII; 216-byte pages let you browse huge files with little RAM.
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
| Page Up/Down | Move 18 entries | Move 216 bytes |
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
