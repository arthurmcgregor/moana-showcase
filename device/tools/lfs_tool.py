#!/usr/bin/env python3
"""
LittleFS tool for the Moana boat unit.

Usage:
  python3 tools/lfs_tool.py list [--port PORT] [--baud BAUD]
  python3 tools/lfs_tool.py download <remote_path> [output_path] [--port PORT] [--baud BAUD]
  python3 tools/lfs_tool.py download-all [output_dir] [--port PORT] [--baud BAUD]
"""

import argparse
import base64
import glob
import os
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)


KNOWN_USB_SERIAL_PATTERNS = [
    "/dev/cu.usbserial*",
    "/dev/cu.SLAB_USBtoUART*",
    "/dev/cu.wchusbserial*",
    "/dev/cu.usbmodem*",
]

READ_TIMEOUT = 10.0   # seconds to wait for a response
LINE_TIMEOUT = 3.0    # seconds of silence before giving up


def find_port():
    for pattern in KNOWN_USB_SERIAL_PATTERNS:
        matches = glob.glob(pattern)
        if matches:
            return matches[0]
    return None


def open_port(port, baud):
    if port is None:
        port = find_port()
        if port is None:
            print("ERROR: Could not auto-detect serial port. Use --port to specify.")
            sys.exit(1)
        print(f"Auto-detected port: {port}")
    ser = serial.Serial(port, baud, timeout=1)
    # Drain any pending input
    time.sleep(0.5)
    ser.reset_input_buffer()
    return ser


def read_lines_until(ser, sentinel, timeout=READ_TIMEOUT):
    """Yield lines from serial until sentinel is found or timeout expires."""
    deadline = time.time() + timeout
    last_line_time = time.time()
    while True:
        line = ser.readline()
        if line:
            last_line_time = time.time()
            decoded = line.decode("utf-8", errors="replace").rstrip("\r\n")
            yield decoded
            if decoded == sentinel:
                return
        else:
            if time.time() - last_line_time > LINE_TIMEOUT:
                print(f"\nWARNING: Timed out waiting for '{sentinel}'")
                return
        if time.time() > deadline:
            print(f"\nWARNING: Overall timeout waiting for '{sentinel}'")
            return


def cmd_list(args):
    ser = open_port(args.port, args.baud)
    ser.write(b"LIST\n")
    print()
    for line in read_lines_until(ser, "LIST_END"):
        if line == "LIST_END":
            break
        print(line)
    ser.close()


def cmd_download(args):
    remote_path = args.remote_path
    if not remote_path.startswith("/"):
        remote_path = "/" + remote_path

    output_path = args.output_path
    if output_path is None:
        output_path = os.path.basename(remote_path)
    if not output_path:
        output_path = "download.bin"

    ser = open_port(args.port, args.baud)
    ser.write(f"DOWNLOAD {remote_path}\n".encode())

    # Wait for DOWNLOAD_START marker
    file_size = None
    deadline = time.time() + READ_TIMEOUT
    while time.time() < deadline:
        line = ser.readline()
        if not line:
            continue
        decoded = line.decode("utf-8", errors="replace").rstrip("\r\n")
        if decoded.startswith("ERROR:"):
            print(decoded)
            ser.close()
            sys.exit(1)
        if decoded.startswith("DOWNLOAD_START:"):
            parts = decoded.split(":")
            # Format: DOWNLOAD_START:<path>:<size>
            # path may contain colons, so size is the last field
            file_size = int(parts[-1])
            print(f"Downloading {remote_path} ({file_size} bytes) → {output_path}")
            break

    if file_size is None:
        print("ERROR: Did not receive DOWNLOAD_START. Is the device running lfs_tool firmware?")
        ser.close()
        sys.exit(1)

    # Collect base64 lines until DOWNLOAD_END
    b64_chunks = []
    for line in read_lines_until(ser, "DOWNLOAD_END", timeout=max(READ_TIMEOUT, file_size / 1000 + 10)):
        if line == "DOWNLOAD_END":
            break
        b64_chunks.append(line)
        received = len(b64_chunks) * 48  # approx, last chunk may be smaller
        progress = min(received, file_size)
        bar_len = 30
        filled = int(bar_len * progress / file_size) if file_size else bar_len
        bar = "#" * filled + "-" * (bar_len - filled)
        print(f"\r[{bar}] {progress}/{file_size} bytes", end="", flush=True)

    ser.close()
    print()  # newline after progress bar

    # Decode and write
    raw = base64.b64decode("".join(b64_chunks))
    with open(output_path, "wb") as f:
        f.write(raw)

    if len(raw) == file_size:
        print(f"OK: {len(raw)} bytes written to {output_path}")
    else:
        print(f"WARNING: Expected {file_size} bytes, got {len(raw)} bytes. File may be incomplete.")


def cmd_download_all(args):
    output_dir = args.output_dir or "."
    os.makedirs(output_dir, exist_ok=True)

    ser = open_port(args.port, args.baud)
    ser.write(b"DOWNLOAD_ALL\n")

    # Wait for DOWNLOAD_ALL_START
    total_files = None
    deadline = time.time() + READ_TIMEOUT
    while time.time() < deadline:
        line = ser.readline()
        if not line:
            continue
        decoded = line.decode("utf-8", errors="replace").rstrip("\r\n")
        if decoded.startswith("ERROR:"):
            print(decoded)
            ser.close()
            sys.exit(1)
        if decoded.startswith("DOWNLOAD_ALL_START:"):
            total_files = int(decoded.split(":")[1])
            print(f"Downloading {total_files} file(s) to {output_dir}/")
            break

    if total_files is None:
        print("ERROR: Did not receive DOWNLOAD_ALL_START.")
        ser.close()
        sys.exit(1)

    files_done = 0
    while files_done < total_files:
        # Wait for DOWNLOAD_START or DOWNLOAD_ALL_END
        file_size = None
        remote_path = None
        deadline = time.time() + READ_TIMEOUT
        while time.time() < deadline:
            line = ser.readline()
            if not line:
                continue
            decoded = line.decode("utf-8", errors="replace").rstrip("\r\n")
            if decoded == "DOWNLOAD_ALL_END":
                ser.close()
                print(f"\nDone. Downloaded {files_done} file(s).")
                return
            if decoded.startswith("DOWNLOAD_START:"):
                parts = decoded.split(":")
                file_size = int(parts[-1])
                remote_path = ":".join(parts[1:-1])
                break

        if file_size is None:
            print("ERROR: Timed out waiting for next DOWNLOAD_START.")
            ser.close()
            sys.exit(1)

        output_path = os.path.join(output_dir, os.path.basename(remote_path))
        print(f"  [{files_done + 1}/{total_files}] {remote_path} ({file_size} bytes) → {output_path}")

        b64_chunks = []
        for line in read_lines_until(ser, "DOWNLOAD_END", timeout=max(READ_TIMEOUT, file_size / 1000 + 10)):
            if line == "DOWNLOAD_END":
                break
            b64_chunks.append(line)
            received = min(len(b64_chunks) * 48, file_size)
            bar_len = 30
            filled = int(bar_len * received / file_size) if file_size else bar_len
            bar = "#" * filled + "-" * (bar_len - filled)
            print(f"\r    [{bar}] {received}/{file_size} bytes", end="", flush=True)

        print()
        raw = base64.b64decode("".join(b64_chunks))
        with open(output_path, "wb") as f:
            f.write(raw)
        if len(raw) != file_size:
            print(f"    WARNING: Expected {file_size} bytes, got {len(raw)}.")
        files_done += 1

    # Drain DOWNLOAD_ALL_END
    for line in read_lines_until(ser, "DOWNLOAD_ALL_END"):
        if line == "DOWNLOAD_ALL_END":
            break

    ser.close()
    print(f"\nDone. Downloaded {files_done} file(s).")


def main():
    parser = argparse.ArgumentParser(description="LittleFS serial tool for the Moana boat unit")
    parser.add_argument("--port", default=None, help="Serial port (auto-detected if omitted)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("list", help="List files on device")

    dl = sub.add_parser("download", help="Download a file from device")
    dl.add_argument("remote_path", help="Path on device (e.g. /track_001.gpx)")
    dl.add_argument("output_path", nargs="?", default=None, help="Local output path (default: basename of remote_path)")

    da = sub.add_parser("download-all", help="Download all files from device")
    da.add_argument("output_dir", nargs="?", default=None, help="Local output directory (default: current directory)")

    args = parser.parse_args()

    if args.command == "list":
        cmd_list(args)
    elif args.command == "download":
        cmd_download(args)
    elif args.command == "download-all":
        cmd_download_all(args)


if __name__ == "__main__":
    main()
