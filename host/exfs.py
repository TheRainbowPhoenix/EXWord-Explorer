"""EXFS/1 USB bridge: fxlink bulk transport and filesystem RPC.

The ctypes layout follows TheRainbowPhoenix/USB_test/fxlink_py.py and the
fxSDK libfxlink headers. No device is needed for importing this module.
"""
from __future__ import annotations

import ctypes as ct
import ctypes.util
import io
import os
import struct
import threading
import time
from pathlib import Path

REQUEST = struct.Struct("<IIHHBB")
RESPONSE = struct.Struct("<IiI")
CMD_LIST, CMD_READ, CMD_CREATE, CMD_WRITE, CMD_DELETE, CMD_RENAME, CMD_STAT, CMD_PING = range(1, 9)
MAX_CHUNK = 512
VOLUMES = frozenset(("drv0", "drv1", "drv2", "crd0", "crd1",
                     "crd2", "sys0", "sys1", "rom0", "rom1"))


class RemoteError(OSError):
    def __init__(self, status: int):
        super().__init__(f"EX-word filesystem error {status}")
        self.status = status


class FxlinkFilter(ct.Structure):
    _fields_ = [("p7", ct.c_bool), ("mass_storage", ct.c_bool),
                ("intf_fxlink", ct.c_bool), ("intf_cesg502", ct.c_bool),
                ("series_cg", ct.c_bool), ("series_g3", ct.c_bool),
                ("serial", ct.c_char_p)]


class FxlinkMessage(ct.Structure):
    _fields_ = [("version", ct.c_uint32), ("size", ct.c_uint32),
                ("transfer_size", ct.c_uint32), ("application", ct.c_char * 16),
                ("type", ct.c_char * 16), ("_padding", ct.c_int),
                ("data", ct.c_void_p)]


class Timeval(ct.Structure):
    _fields_ = [("tv_sec", ct.c_long), ("tv_usec", ct.c_long)]


def remote_path(local: str | bytes) -> bytes:
    """Convert a user-visible /drv0/path to native \\drv0\path.
    A TFTP URL cannot access host paths or escape from a device volume.
    """
    if isinstance(local, bytes):
        local = local.decode("utf-8", errors="strict")
    if "\\" in local or "\x00" in local:
        raise ValueError("Use POSIX / separators in remote paths")
    elements = local.strip("/").split("/")
    if not elements or elements[0].lower() not in VOLUMES:
        raise ValueError("Remote path must begin with a recognized EX-word volume")
    if any(part in ("", ".", "..") for part in elements):
        raise ValueError("Empty path segments and parent traversal are forbidden")
    raw = ("\\\\" + "\\".join(elements)).encode("shift_jis")
    if len(raw) >= 320:
        raise ValueError("EX-word path exceeds 319 bytes")
    return raw


def display_path(native: bytes) -> str:
    """Native paths are Shift-JIS; preserve undecodable byte escapes."""
    return native.decode("shift_jis", errors="backslashreplace").replace("\\", "/")


class FxlinkUSB:
    def __init__(self, library: str | None = None):
        root = Path(__file__).resolve().parent
        guesses = ([library] if library else [
            str(root / "libfxlink.so"), str(root / "libfxlink.dll"),
            str(root / "libfxlink.dylib"), ctypes_library("fxlink")])
        self.fx = next((ct.CDLL(s) for s in guesses if s and can_load(s)), None)
        if self.fx is None:
            raise RuntimeError("libfxlink not found: install libfxlink or use the CI host artifact")
        usb_names = ([ctypes_library("usb-1.0"), "libusb-1.0.so.0",
                      "libusb-1.0.dll", "libusb-1.0.dylib"])
        self.usb = next((ct.CDLL(s) for s in usb_names if s and can_load(s)), None)
        if self.usb is None:
            raise RuntimeError("libusb-1.0 not found")

        self.usb.libusb_init.argtypes = [ct.POINTER(ct.c_void_p)]
        self.usb.libusb_init.restype = ct.c_int
        self.usb.libusb_exit.argtypes = [ct.c_void_p]
        self.usb.libusb_handle_events_timeout_completed.argtypes = [
            ct.c_void_p, ct.POINTER(Timeval), ct.c_void_p]
        self.usb.libusb_handle_events_timeout_completed.restype = ct.c_int

        self.fx.fxlink_device_find.argtypes = [ct.c_void_p, ct.POINTER(FxlinkFilter)]
        self.fx.fxlink_device_find.restype = ct.c_void_p
        self.fx.fxlink_device_claim_fxlink.argtypes = [ct.c_void_p]
        self.fx.fxlink_device_claim_fxlink.restype = ct.c_bool
        self.fx.fxlink_device_start_bulk_IN.argtypes = [ct.c_void_p]
        self.fx.fxlink_device_start_bulk_IN.restype = ct.c_bool
        self.fx.fxlink_device_finish_bulk_IN.argtypes = [ct.c_void_p]
        self.fx.fxlink_device_finish_bulk_IN.restype = ct.POINTER(FxlinkMessage)
        self.fx.fxlink_device_send_bulk_OUT.argtypes = [
            ct.c_void_p, ct.c_void_p, ct.c_char_p, ct.c_char_p,
            ct.c_void_p, ct.c_int]
        self.fx.fxlink_device_send_bulk_OUT.restype = ct.c_bool
        self.fx.fxlink_message_free.argtypes = [ct.POINTER(FxlinkMessage), ct.c_bool]
        self.fx.fxlink_device_cleanup.argtypes = [ct.c_void_p]
        self.ctx = ct.c_void_p()
        if self.usb.libusb_init(ct.byref(self.ctx)) != 0:
            raise RuntimeError("libusb initialization failed")
        self.dev = None
        self.lock = threading.Lock()
        self.sequence = 0

    def _events(self):
        tv = Timeval(0, 100_000)
        rc = self.usb.libusb_handle_events_timeout_completed(
            self.ctx, ct.byref(tv), None)
        if rc < 0:
            raise ConnectionError(f"libusb event loop failed: {rc}")

    def connect(self, timeout=15):
        deadline = time.monotonic() + timeout
        flt = FxlinkFilter()
        flt.intf_fxlink = True
        while time.monotonic() < deadline:
            self.dev = self.fx.fxlink_device_find(self.ctx, ct.byref(flt))
            if self.dev:
                break
            self._events()
        if not self.dev:
            raise TimeoutError("No fxlink-compatible EX-word found; start USB Remote on device")
        if not self.fx.fxlink_device_claim_fxlink(self.dev):
            raise ConnectionError("Unable to claim USB interface (check permissions/WinUSB)")
        if not self.fx.fxlink_device_start_bulk_IN(self.dev):
            raise ConnectionError("Could not start bulk input")

    def exchange(self, cmd: int, path: bytes, offset=0, length=0, data=b"",
                 flags=0, timeout=15):
        if not self.dev:
            raise ConnectionError("Connect the USB device first")
        if not 0 < len(path) < 320 or not 0 <= length <= 512:
            raise ValueError("Invalid request path/size")
        if not 0 <= offset < 2**31:
            raise ValueError("File offset outside EX-word range")
        if len(data) > 512:
            raise ValueError("Payload too large")
        with self.lock:
            self.sequence = (self.sequence + 1) & 0xffffffff
            ident = self.sequence
            payload = REQUEST.pack(ident, offset, length, len(path),
                                   cmd, flags) + path + data
            buffer = ct.create_string_buffer(payload)
            ok = self.fx.fxlink_device_send_bulk_OUT(
                self.ctx, self.dev, b"exfs", b"req",
                buffer, len(payload))
            if not ok:
                raise ConnectionError("fxlink USB send failed")
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                self._events()
                msg_ptr = self.fx.fxlink_device_finish_bulk_IN(self.dev)
                if not msg_ptr:
                    continue
                try:
                    msg = msg_ptr.contents
                    if msg.size > 1024:
                        raise ConnectionError("Oversized EXFS response")
                    raw = ct.string_at(msg.data, msg.size)
                    application = bytes(msg.application).rstrip(b"\x00")
                    typ = bytes(msg.type).rstrip(b"\x00")
                finally:
                    self.fx.fxlink_message_free(msg_ptr, True)
                    if not self.fx.fxlink_device_start_bulk_IN(self.dev):
                        raise ConnectionError("Could not restart fxlink receiver")
                if application != b"exfs" or typ != b"resp" or len(raw) < 12:
                    continue
                number, status, total = RESPONSE.unpack_from(raw)
                if number != ident:
                    continue
                if status < 0:
                    raise RemoteError(status)
                return total, raw[12:]
            raise TimeoutError("No EXFS response; check USB connectivity")

    def close(self):
        try:
            if self.dev:
                self.fx.fxlink_device_cleanup(self.dev)
                self.dev = None
        finally:
            if self.ctx:
                self.usb.libusb_exit(self.ctx)
                self.ctx = None


def ctypes_library(name):
    return ctypes.util.find_library(name)


def can_load(name):
    try:
        ct.CDLL(name)
        return True
    except OSError:
        return False


class Filesystem:
    def __init__(self, transport):
        self.transport = transport

    def command(self, command, path, offset=0, length=0, data=b"", flags=0):
        raw = remote_path(path) if not isinstance(path, bytes) else path
        return self.transport.exchange(command, raw, offset, length, data, flags)

    def list(self, directory):
        names = []
        offset = 0
        while True:
            total, body = self.command(CMD_LIST, directory, offset=offset)
            pointer = 0
            while pointer < len(body):
                if pointer+2 > len(body):
                    raise IOError("Truncated listing")
                kind, n = body[pointer], body[pointer+1]
                pointer += 2
                if pointer+n > len(body):
                    raise IOError("Truncated entry")
                name_bytes = body[pointer:pointer+n]
                pointer += n
                names.append((bool(kind), name_bytes.decode("shift_jis",
                                errors="replace"), name_bytes))
            if len(names) >= total:
                return names
            if len(names) == offset:
                raise IOError("Directory listing made no progress")
            offset = len(names)

    def stat(self, path):
        size, _ = self.command(CMD_STAT, path)
        return size

    def read(self, path, offset, length=512):
        size, content = self.command(CMD_READ, path, offset,
                                     min(length, MAX_CHUNK))
        return size, content

    def download(self, path, target, progress=None):
        total = self.stat(path)
        count = 0
        while count < total:
            _, chunk = self.read(path, count)
            if not chunk:
                raise IOError("Unexpected EOF during USB download")
            target.write(chunk)
            count += len(chunk)
            if progress:
                progress(count, total)
        return count

    def upload(self, path, source, progress=None):
        # No overwriting. To replace a file, explicitly delete it first.
        self.command(CMD_CREATE, path)
        done = 0
        try:
            while True:
                chunk = source.read(MAX_CHUNK)
                if not chunk:
                    break
                self.command(CMD_WRITE, path, offset=done,
                             length=len(chunk), data=chunk)
                done += len(chunk)
                if progress:
                    progress(done, None)
        except Exception:
            try:
                self.remove(path)
            except Exception:
                pass
            raise
        return done

    def mkdir(self, path):
        self.command(CMD_CREATE, path, flags=1)

    def remove(self, path):
        self.command(CMD_DELETE, path)

    def rename(self, old, new):
        target = new if isinstance(new, bytes) else remote_path(new)
        if len(target) > MAX_CHUNK:
            raise ValueError("Destination path too large")
        self.command(CMD_RENAME, old, length=len(target), data=target)

    def ping(self):
        return self.command(CMD_PING, "/drv0")[1] == b"EXFS/1"
