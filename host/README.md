# EXWord Explorer - USB Remote

The device has no native IP/TFTP stack. The EX-word uses USB bulk fxlink
and a small EXFS/1 binary request/response protocol. The PC Python bridge
offers a **localhost TFTP server** and a **desktop Tkinter file manager**,
translating TFTP requests to USB filesystem operations.

## Getting started

1. Install EXWord Explorer on the EX-word and open its volume home screen.
2. Select **USB Remote** and press EXE. Connect the USB cable.
3. Download the GitHub Actions **EXWord-Explorer-host-linux** artifact,
   unzip it, and run: python3 bridge.py
4. The Tkinter GUI opens, and TFTP is served at 127.0.0.1:6969.

Host artifact includes bridge.py, exfs.py and the compiled libfxlink.so.
You need Python 3.10+, libusb-1.0 and Tkinter for the GUI. On Linux you
might need udev permissions. For Windows, download the EXWord-Explorer-host-windows artifact from the
same CI run. It includes libfxlink.dll, libusb-1.0.dll and the Python
scripts; place them together and run python bridge.py. If DLL loading
fails, make sure a compatible 64-bit Python interpreter and required
UCRT libraries are available, or supply --libfxlink explicitly.

Options:
- --no-gui: TFTP server without the desktop interface.
- --no-tftp: desktop interface only.
- --allow-write: enable uploading via TFTP (disabled by default).
- --port 6969: choose a port, still bound to 127.0.0.1 only.

The Tkinter GUI supports directory browsing, Send/Receive, mkdir,
rename and delete. The USB interface serializes requests; one device at
a time. The GUI requests confirmation before deletion.

## Standard TFTP interoperability

Any standard TFTP client in octet/binary mode can contact 127.0.0.1 on
port 6969. An example remote file is drv0/CASIO/sample.dat.
A virtual text file named .exword-list in any remote directory
(e.g. drv0/CASIO/.exword-list) lists its directory entries for command
line TFTP clients. This virtual file does not exist on the EX-word.
TFTP has no directory listing or rename/delete/mkdir operations; those
are provided by the desktop GUI.

Uploads must target non-existing files and require --allow-write.
The host will never overwrite existing files automatically. TFTP uses 512-byte network blocks, while USB uses 2048-byte chunks
with host-side caching to reduce USB round trips. The GUI streams to
disk, displays bytes transferred, and offers Cancel. Cancellation takes
effect between USB chunks. A failed upload tries
to delete its partial target. Avoid working on firmware/system volumes.

## Wire protocol: EXFS/1

fxlink message application is "exfs"; type is "req" for host-to-device,
"resp" for device-to-host. All fields are little-endian. This is NOT
TFTP on the USB link; only the PC endpoint speaks TFTP.

Request header has:
  u32 request_id
  u32 offset (byte offset, or directory entry index for LIST)
  u16 length
  u16 native_path_length
  u8  command (1 LIST, 2 READ, 3 CREATE, 4 WRITE, 5 DELETE,
               6 RENAME, 7 STAT, 8 PING, 9 BEGIN)
  u8  flags (CREATE bit 0 = folder, BEGIN bit 0 = uploading)
  followed by native path bytes and, for WRITE/RENAME, payload bytes.

Response header has u32 request_id, i32 status, u32 total, followed
by response bytes. status < 0 indicates a native filesystem error.
total is directory entry count for LIST, file size for STAT/READ.
LIST entries are [u8 is_directory, u8 name_length, name_bytes...].
EXFS request data chunks are limited to 2048 bytes; messages include
the path and header in addition to that data. BEGIN uses the offset
field as the complete transfer size (bytes), length 0, and the path
being transferred; a size of 0 means unknown total. READ and WRITE
responses then advance the progress shown on the device screen. Native
paths must start with a valid volume (drv0/data0/crd0/sys0/etc).

## Security and hardware limitations

The server binds only to localhost and does NOT provide authentication.
Never forward the port outside the computer. Device writes to sys0 or
other firmware volumes are risky and not guaranteed to be possible.

The USB remote implementation is compiled and linked in CI, but the
EX-word USB/OS filesystem coexistence must be verified on hardware.
The USB_test project exercises fxlink mostly in its fx-CP demo; that is
not proof that the same driver runs reliably on EX-word. Test read-only
browsing and downloads before testing uploads.

No firmware protections are bypassed. Hidden objects not returned by
native EX-word enumeration remain inaccessible. TFTP uses 512-byte
blocks and 16-bit block numbers; it is intended for normal sized files.

## Troubleshooting / verbose output

Run `python3 bridge.py --verbose` (or `-v`) to print the active
libfxlink/libusb library, USB discovery and claim stages, request IDs,
commands, native paths, sizes, response status values, ignored packets,
and request timeouts. Verbose output goes to stderr; it does not dump
file contents.

**Fixed:** The original host code called
`fxlink_device_start_bulk_IN()` after every response, even though
libfxlink's receive callback already resubmits its bulk-IN transfer.
This triggered `Could not restart fxlink receiver` immediately after
a successful 18-byte `exfs:resp` ping. The receiver is now started
once at connection and remains active automatically. You only need
to update the **host Python scripts** for this fix; the device firmware
is unchanged for the receive-lifecycle issue. To browse `data0`,
update both host scripts and the EX-word package.

## Faster transfers and cancellation

TFTP's 512-byte DATA payload is a protocol requirement for classic
RFC 1350 transfers, not the EXFS USB chunk size. The bridge now reads
up to 2048 bytes per USB RPC and serves multiple 512-byte TFTP packets
from the resulting cache. Writes combine up to four TFTP blocks before
sending to the EX-word. Ordinary host GUI uploads/downloads stream
2048-byte chunks directly from/to files without keeping whole files
in memory.

The GUI has a transfer progress bar and **Cancel** control. Download
cancellation removes the temporary `.partial-exword` file; cancelling
an upload attempts to delete the incomplete remote target. It is safe
to ask for cancellation but the current USB transfer must finish first.
On-device USB mode has a bottom-right **STOP USB** touch area plus
ESC / AC-ON / Sound key. Device USB receives use a bounded timeout to
avoid trapping the program indefinitely on a partial host message.

The Windows libfxlink DLL in CI is compiled with
`FXLINK_DISABLE_ANSIESC=ON` to avoid colored escape sequences in
traditional Windows consoles. Update the DLL as well as both Python
files to benefit from all these changes.

## Official EX-word volumes

The device and host list the eight registered backends in this order:
`drv0`, `sys0`, `data0`, `data1`, `data2`, `crd0`, `crd1`,
`crd2`. The native `findfirst` error -1 is interpreted as an empty
folder in the browser; access restrictions on protected volumes are not
bypassed.

## USB dashboard and Windows scrollbars

The landscape EX-word USB screen includes a QR code opening
**http://pho3.be/go/exxplorer**, the PC-side TFTP endpoint
`127.0.0.1:6969`, request counter, file path, and current file progress
(`12 / 52 chunks`, for example). The device itself does not provide
an FTP server or an IP address; the TFTP bridge runs on the computer.
A new EXFS `BEGIN` request (opcode 9) announces a transfer's size and
direction before streaming. For classic TFTP WRQ clients with no size
information, the display shows the current chunk with an unknown total.

The Tkinter GUI now includes both a vertical directory-list scrollbar
and a horizontal scrollbar for long filenames. On Windows, update
`bridge.py` and `exfs.py` together with the device add-in: the
on-device transfer dashboard needs support for BEGIN in both ends.
