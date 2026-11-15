"""Desktop explorer and localhost TFTP server for the EXFS/1 fxlink bridge.

Run: python3 host/bridge.py             (GUI + read-only TFTP)
     python3 host/bridge.py --allow-write  (TFTP upload enabled)
     python3 host/bridge.py --no-gui       (headless TFTP)

TFTP is bound to 127.0.0.1:6969 by default, never exposed to the LAN.
Standard TFTP does not support browsing folders. Request a virtual file named
.exword-list within a folder to get a human-readable directory listing.
"""
from __future__ import annotations

import argparse
import io
import os
import queue
import socket
import socketserver
import struct
import threading
import time
from pathlib import Path

from exfs import Filesystem, FxlinkUSB, VOLUMES

BLOCK = 512
TIMEOUT = 3
RETRIES = 4


def tftp_name(packet):
    chunks = packet[2:].split(b"\x00")
    if len(chunks) < 3:
        raise ValueError("Malformed TFTP request")
    name = chunks[0].decode("utf-8", errors="strict")
    mode = chunks[1].decode("ascii", errors="strict").lower()
    if mode != "octet":
        raise ValueError("Only octet/binary TFTP transfers supported")
    if not name:
        raise ValueError("Empty filename")
    return "/" + name.lstrip("/")


def listing_text(filesystem, path):
    lines = []
    for directory, label, _ in filesystem.list(path):
        lines.append(("[DIR]  " if directory else "[FILE] ") + label)
    return ("\n".join(lines) + "\n").encode("utf-8")


def packet_error(code, message):
    return struct.pack("!HH", 5, code) + str(message).encode("utf-8")[:170] + b"\x00"


class TFTPHandler(socketserver.BaseRequestHandler):
    def handle(self):
        packet, _ = self.request
        if len(packet) < 4:
            return
        op = struct.unpack_from("!H", packet)[0]
        if op not in (1, 2):
            return
        try:
            path = tftp_name(packet)
            if op == 2 and not self.server.allow_write:
                raise PermissionError("TFTP writes disabled; launch with --allow-write")
            # Use a fresh transfer ID as required by RFC 1350.
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.bind((self.server.server_address[0], 0))
                sock.settimeout(TIMEOUT)
                if op == 1:
                    self.rrq(sock, path)
                else:
                    self.wrq(sock, path)
        except Exception as exc:
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as error:
                    error.sendto(packet_error(2, exc), self.client_address)
            except OSError:
                pass

    def rrq(self, sock, path):
        filesystem = self.server.fs
        source = None
        size = None
        if path.endswith("/.exword-list"):
            source = listing_text(filesystem, path.rsplit("/", 1)[0])
            size = len(source)
        else:
            size = filesystem.stat(path)
        offset, block = 0, 1
        while True:
            if source is not None:
                data = source[offset:offset+BLOCK]
            else:
                total, data = filesystem.read(path, offset)
                if total != size:
                    raise IOError("File size changed during transfer")
            payload = struct.pack("!HH", 3, block) + data
            acknowledged = False
            for _ in range(RETRIES):
                sock.sendto(payload, self.client_address)
                try:
                    response, peer = sock.recvfrom(516)
                except socket.timeout:
                    continue
                if peer != self.client_address or len(response) < 4:
                    continue
                opcode, received = struct.unpack_from("!HH", response)
                if opcode == 5:
                    return
                if opcode == 4 and received == block:
                    acknowledged = True
                    break
            if not acknowledged:
                raise TimeoutError("No TFTP ACK")
            offset += len(data)
            block = (block+1) & 0xffff
            # For a multiple of 512 bytes a final zero-length packet follows.
            if len(data) < BLOCK:
                return

    def wrq(self, sock, path):
        if path.endswith("/.exword-list"):
            raise ValueError("Cannot overwrite the virtual directory listing")
        filesystem = self.server.fs
        filesystem.command(3, path)  # create without overwriting
        ack = struct.pack("!HH", 4, 0)
        expected = 1
        offset = 0
        try:
            while True:
                response = None
                for _ in range(RETRIES):
                    sock.sendto(ack, self.client_address)
                    try:
                        data, peer = sock.recvfrom(516)
                    except socket.timeout:
                        continue
                    if peer != self.client_address or len(data) < 4:
                        continue
                    opcode, seq = struct.unpack_from("!HH", data)
                    if opcode == 5:
                        return
                    if opcode == 3 and seq == expected:
                        response = data[4:]
                        break
                if response is None:
                    raise TimeoutError("No TFTP DATA packet")
                if response:
                    filesystem.command(4, path, offset, len(response), response)
                offset += len(response)
                ack = struct.pack("!HH", 4, expected)
                expected = (expected+1) & 0xffff
                if len(response) < BLOCK:
                    sock.sendto(ack, self.client_address)
                    # Wait for one duplicate final DATA packet, then re-ACK.
                    sock.settimeout(0.3)
                    try:
                        dup, peer = sock.recvfrom(516)
                        if peer == self.client_address and dup[:4] == struct.pack(
                                "!HH", 3, (expected-1)&0xffff):
                            sock.sendto(ack, peer)
                    except socket.timeout:
                        pass
                    return
        except Exception:
            try:
                filesystem.remove(path)  # remove partial uploads
            except Exception:
                pass
            raise


class TFTPServer(socketserver.ThreadingMixIn, socketserver.UDPServer):
    daemon_threads = True
    allow_reuse_address = True
    def __init__(self, address, filesystem, allow_write=False):
        super().__init__(address, TFTPHandler)
        self.fs = filesystem
        self.allow_write = allow_write


def gui(filesystem):
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox, simpledialog

    root = tk.Tk()
    root.title("EXWord Explorer - USB")
    root.geometry("900x620")
    current = tk.StringVar(value="/drv0")
    status = tk.StringVar(value="Connected")
    tasks = queue.Queue()
    busy = False
    volume = tk.StringVar(value="drv0")

    toolbar = ttk.Frame(root, padding=8)
    toolbar.pack(fill="x")
    ttk.Label(toolbar, text="Volume:").pack(side="left")
    volumes = ttk.Combobox(toolbar, values=sorted(VOLUMES),
                           textvariable=volume, width=7, state="readonly")
    volumes.pack(side="left", padx=6)
    path_label = ttk.Label(toolbar, textvariable=current)
    path_label.pack(side="left", fill="x", expand=True, padx=8)
    filelist = ttk.Treeview(root, columns=("type", "name"), show="headings",
                            selectmode="browse")
    filelist.heading("type", text="Type")
    filelist.heading("name", text="Name (hidden files included)")
    filelist.column("type", width=105, stretch=False)
    filelist.column("name", width=680)
    filelist.pack(fill="both", expand=True, padx=8)
    bottom = ttk.Frame(root, padding=8)
    bottom.pack(fill="x")
    ttk.Label(root, textvariable=status, anchor="w", padding=7).pack(
        side="bottom", fill="x")
    lookup = {}

    def run(action, finish=None):
        nonlocal busy
        if busy:
            return
        busy = True
        status.set("Working...")
        def thread():
            try:
                result = action()
                tasks.put((True, result, finish))
            except Exception as exc:
                tasks.put((False, str(exc), finish))
        threading.Thread(target=thread, daemon=True).start()

    def pump():
        nonlocal busy
        try:
            while True:
                ok, value, finish = tasks.get_nowait()
                busy = False
                if not ok:
                    messagebox.showerror("EX-word transfer error", value)
                    status.set("Error: " + value)
                else:
                    status.set("Ready")
                    if finish:
                        finish(value)
        except queue.Empty:
            pass
        root.after(60, pump)

    def refresh():
        target = current.get()
        def update(result):
            lookup.clear()
            filelist.delete(*filelist.get_children())
            for i, (isdir, name, raw) in enumerate(result):
                iid = str(i)
                lookup[iid] = (isdir, name)
                filelist.insert("", "end", iid=iid,
                                values=("Folder" if isdir else "File", name))
            status.set(f"{len(result)} entries - {target}")
        run(lambda: filesystem.list(target), update)

    def selected():
        sel = filelist.selection()
        return lookup.get(sel[0]) if sel else None

    def enter(event=None):
        item = selected()
        if item and item[0]:
            current.set(current.get().rstrip("/") + "/" + item[1])
            refresh()
        elif item:
            receive()

    def up():
        path = current.get()
        components = path.strip("/").split("/")
        if len(components) > 1:
            current.set("/" + "/".join(components[:-1]))
            refresh()

    def receive():
        item = selected()
        if not item or item[0]:
            return
        local = filedialog.asksaveasfilename(initialfile=item[1])
        if not local:
            return
        remote = current.get().rstrip("/") + "/" + item[1]
        def download():
            # Write only complete downloads to the chosen path.
            tmp = local + ".partial-exword"
            try:
                with open(tmp, "wb") as output:
                    filesystem.download(remote, output)
                os.replace(tmp, local)
            finally:
                if os.path.exists(tmp):
                    os.remove(tmp)
        run(download, lambda _: status.set("Received " + item[1]))

    def send():
        local = filedialog.askopenfilename()
        if not local:
            return
        name = os.path.basename(local)
        remote = current.get().rstrip("/") + "/" + name
        def upload():
            with open(local, "rb") as source:
                filesystem.upload(remote, source)
        run(upload, lambda _: refresh())

    def make_folder():
        name = simpledialog.askstring("New folder", "Folder name:")
        if name:
            run(lambda: filesystem.mkdir(current.get().rstrip("/") + "/" + name),
                lambda _: refresh())

    def delete():
        item = selected()
        if item and messagebox.askyesno("Delete", f"Permanently delete {item[1]}?"):
            remote = current.get().rstrip("/") + "/" + item[1]
            run(lambda: filesystem.remove(remote), lambda _: refresh())

    def rename():
        item = selected()
        if not item:
            return
        name = simpledialog.askstring("Rename", "New name:", initialvalue=item[1])
        if name and name != item[1]:
            base = current.get().rstrip("/") + "/"
            run(lambda: filesystem.rename(base + item[1], base + name),
                lambda _: refresh())

    def change_volume(event=None):
        current.set("/" + volume.get())
        refresh()

    for title, function in (("Up", up), ("Refresh", refresh),
                            ("Receive", receive), ("Send", send),
                            ("New folder", make_folder), ("Rename", rename),
                            ("Delete", delete)):
        ttk.Button(bottom, text=title, command=function).pack(
            side="left", padx=4)
    volumes.bind("<<ComboboxSelected>>", change_volume)
    filelist.bind("<Double-1>", enter)
    root.after(60, pump)
    refresh()
    root.mainloop()


def main(argv=None):
    parser = argparse.ArgumentParser(description="EX-word USB filesystem bridge")
    parser.add_argument("--libfxlink", help="Path to libfxlink shared library")
    parser.add_argument("--no-gui", action="store_true", help="Only run TFTP")
    parser.add_argument("--no-tftp", action="store_true", help="Only show GUI")
    parser.add_argument("--allow-write", action="store_true",
                        help="Enable TFTP uploads (never overwrite)")
    parser.add_argument("--port", type=int, default=6969,
                        help="Local TFTP port (default 6969)")
    parser.add_argument("--host", default="127.0.0.1",
                        choices=["127.0.0.1"],
                        help="Bind to localhost only")
    parser.add_argument("--timeout", type=int, default=15)
    opts = parser.parse_args(argv)
    if opts.no_gui and opts.no_tftp:
        parser.error("One of GUI or TFTP must remain enabled")
    transport = FxlinkUSB(opts.libfxlink)
    server = None
    try:
        transport.connect(timeout=opts.timeout)
        fs = Filesystem(transport)
        if not fs.ping():
            raise ConnectionError("Connected USB endpoint does not speak EXFS/1")
        if not opts.no_tftp:
            server = TFTPServer((opts.host, opts.port), fs, opts.allow_write)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            print(f"TFTP serving {opts.host}:{opts.port}; uploads "+
                  ("enabled" if opts.allow_write else "disabled"))
        if opts.no_gui:
            try:
                while True:
                    time.sleep(0.5)
            except KeyboardInterrupt:
                pass
        else:
            gui(fs)
    finally:
        if server:
            server.shutdown()
            server.server_close()
        transport.close()


if __name__ == "__main__":
    main()
