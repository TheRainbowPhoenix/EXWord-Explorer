"""Offline EXFS/1 client and TFTP tests; no USB hardware necessary."""
import io
import ctypes as ct
import os
import socket
import struct
import sys
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from exfs import (Filesystem, RemoteError, remote_path,
                  FxlinkUSB, FxlinkMessage,
                  CMD_LIST, CMD_READ, CMD_CREATE, CMD_WRITE,
                  CMD_DELETE, CMD_RENAME, CMD_STAT, CMD_PING)
from bridge import TFTPServer


class FakeTransport:
    def __init__(self):
        self.files = {"\\\\drv0\\bin.dat": b"\x00hello\xff" * 80}
        self.folders = {"\\\\drv0"}
        self.calls = []

    def exchange(self, cmd, path, offset=0, length=0, data=b"", flags=0):
        if isinstance(path, bytes):
            path = path.decode("shift_jis")
        self.calls.append((cmd, path))
        if cmd == CMD_PING:
            return 0, b"EXFS/1"
        if cmd == CMD_STAT:
            if path not in self.files:
                raise RemoteError(-1)
            return len(self.files[path]), b""
        if cmd == CMD_READ:
            if path not in self.files:
                raise RemoteError(-1)
            b = self.files[path]
            return len(b), b[offset:offset+length]
        if cmd == CMD_CREATE:
            if flags & 1:
                self.folders.add(path)
            else:
                if path in self.files:
                    raise RemoteError(-13)
                self.files[path] = b""
            return 0, b""
        if cmd == CMD_WRITE:
            if path not in self.files:
                raise RemoteError(-1)
            b = self.files[path]
            if offset > len(b):
                raise RemoteError(-19)
            self.files[path] = b[:offset] + data + b[offset+len(data):]
            return 0, b""
        if cmd == CMD_DELETE:
            self.files.pop(path)
            return 0, b""
        if cmd == CMD_RENAME:
            target = data.decode("shift_jis")
            self.files[target] = self.files.pop(path)
            return 0, b""
        if cmd == CMD_LIST:
            entries = sorted(
                [(True, p.rsplit("\\", 1)[-1]) for p in self.folders
                 if p.startswith(path + "\\") and
                 "\\" not in p[len(path)+1:]] +
                [(False, p.rsplit("\\", 1)[-1]) for p in self.files
                 if p.startswith(path + "\\") and
                 "\\" not in p[len(path)+1:]])
            payload = b""
            for directory, name in entries[offset:offset+18]:
                raw = name.encode("shift_jis")
                payload += bytes((1 if directory else 0, len(raw))) + raw
            return len(entries), payload
        raise RemoteError(-99)


class ProtocolTests(unittest.TestCase):
    def test_bad_paths(self):
        for path in ["/drv0/../sysram", "drv0//x", "/etc/passwd",
                     "../drv0", "/drv0/a\\b"]:
            with self.subTest(path=path):
                with self.assertRaises(ValueError):
                    remote_path(path)

    def test_paths(self):
        self.assertEqual(remote_path("/drv0/test.dat"), b"\\\\drv0\\test.dat")

    def test_data0_and_crd0_volumes(self):
        self.assertEqual(remote_path("/data0"), b"\\\\data0")
        self.assertEqual(remote_path("/data0/foo.dat"), b"\\\\data0\\foo.dat")
        self.assertEqual(remote_path("/crd0"), b"\\\\crd0")

    def test_fxlink_receiver_is_not_restarted(self):
        """libfxlink auto-resubmits IN; calling start_bulk_IN after reply
        always fails because there is already a pending IN transfer."""
        class AutoRearmingFxlink:
            def __init__(self):
                self.pending = []
                self.saved = []
                self.starts = 0
                self.freed = 0
            def fxlink_device_send_bulk_OUT(self, ctx, dev, app, typ, buffer, size):
                self.last_request = ct.string_at(buffer, size)
                sequence = struct.unpack_from("<I", self.last_request)[0]
                raw = struct.pack("<IiI", sequence, 0, 0) + b"EXFS/1"
                payload = ct.create_string_buffer(raw)
                message = FxlinkMessage()
                message.size = len(raw)
                message.application = b"exfs"
                message.type = b"resp"
                message.data = ct.cast(payload, ct.c_void_p).value
                self.saved.append((payload, message))
                self.pending.append(ct.pointer(message))
                return True
            def fxlink_device_finish_bulk_IN(self, dev):
                return self.pending.pop(0) if self.pending else None
            def fxlink_message_free(self, message, free_data):
                self.freed += 1
            def fxlink_device_start_bulk_IN(self, dev):
                self.starts += 1
                return False  # receiver is already active

        usb = FxlinkUSB.__new__(FxlinkUSB)
        usb.verbose = False
        usb.dev = object()
        usb.ctx = object()
        usb.lock = threading.Lock()
        usb.sequence = 0
        usb.fx = AutoRearmingFxlink()
        usb._events = lambda: None
        for _ in range(3):
            self.assertEqual(usb.exchange(CMD_PING, remote_path("/drv0")),
                             (0, b"EXFS/1"))
        self.assertEqual(usb.fx.freed, 3)
        self.assertEqual(usb.fx.starts, 0)

    def test_file_roundtrip_and_listing(self):
        fake = FakeTransport()
        fs = Filesystem(fake)
        self.assertTrue(fs.ping())
        listing = fs.list("/drv0")
        self.assertIn((False, "bin.dat", b"bin.dat"), listing)
        binary = io.BytesIO()
        fs.download("/drv0/bin.dat", binary)
        self.assertEqual(binary.getvalue(), fake.files["\\\\drv0\\bin.dat"])
        fs.upload("/drv0/new.dat", io.BytesIO(b"\x00\xff" * 1024))
        self.assertEqual(fake.files["\\\\drv0\\new.dat"], b"\x00\xff" * 1024)
        with self.assertRaises(RemoteError):
            fs.upload("/drv0/new.dat", io.BytesIO(b"overwrite"))
        fs.rename("/drv0/new.dat", "/drv0/renamed.bin")
        self.assertIn("\\\\drv0\\renamed.bin", fake.files)
        fs.remove("/drv0/renamed.bin")


class TFTPTests(unittest.TestCase):
    def setUp(self):
        self.fake = FakeTransport()
        self.fs = Filesystem(self.fake)
        self.server = TFTPServer(("127.0.0.1", 0), self.fs, allow_write=True)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.address = self.server.server_address

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()

    def test_rrq_binary_and_directory_index(self):
        for name in [b"drv0/bin.dat", b"drv0/.exword-list"]:
            with self.subTest(name=name), socket.socket(socket.AF_INET,
                                                         socket.SOCK_DGRAM) as s:
                s.settimeout(3)
                s.sendto(struct.pack("!H", 1) + name + b"\x00octet\x00",
                         self.address)
                got = bytearray()
                expected = 1
                while True:
                    packet, peer = s.recvfrom(520)
                    op, seq = struct.unpack_from("!HH", packet)
                    self.assertEqual((op, seq), (3, expected))
                    got.extend(packet[4:])
                    s.sendto(struct.pack("!HH", 4, expected), peer)
                    expected = (expected + 1) & 0xffff
                    if len(packet[4:]) < 512:
                        break
                if name.endswith(b"bin.dat"):
                    self.assertEqual(bytes(got), self.fake.files["\\\\drv0\\bin.dat"])
                else:
                    self.assertIn(b"bin.dat", got)

    def test_wrq_binary(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.settimeout(3)
            s.sendto(struct.pack("!H", 2) + b"drv0/upload.bin\x00octet\x00",
                     self.address)
            reply, peer = s.recvfrom(520)
            self.assertEqual(reply, struct.pack("!HH", 4, 0))
            content = b"\x00\x01\xff" * 250
            for block in (1, 2):
                chunk = content[(block-1)*512:block*512]
                s.sendto(struct.pack("!HH", 3, block) + chunk, peer)
                reply, _ = s.recvfrom(520)
                self.assertEqual(reply, struct.pack("!HH", 4, block))
            self.assertEqual(self.fake.files["\\\\drv0\\upload.bin"], content)


if __name__ == "__main__":
    unittest.main()
