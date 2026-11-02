#!/usr/bin/env python3
"""Firmware updates of remote-a displays over RS485, from this computer.

The RS485 plugin of Signal K (semantic-boat-devices-rs485) owns the serial
port; this tool talks to it through its local control socket. Needs only
Python 3 (no packages). One-step build and update: script/remote-a-update.sh.

    remote-a-update.py list                    displays known to the plugin
    remote-a-update.py status remote-a-1       running firmware (or bootloader)
    remote-a-update.py bus                     RS485 bus counters (replies, timeouts, errors)
    remote-a-update.py update remote-a-1 [remote-a-2 ...] [--image remote-a.bin]
    remote-a-update.py update all              every display that is online

The image is the stamped remote-a.bin of the RS485 build (default:
build/remote-a-rs485/release/src/dev/remote-a/remote-a.bin). An update
restarts the display into its bootloader, sends the image (about 30 s), and
waits until the new firmware runs and has confirmed itself. Exit status 0
only if every update succeeded.
"""

import argparse
import json
import os
import pathlib
import socket
import sys

REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_IMAGE = REPOSITORY / "build/remote-a-rs485/release/src/dev/remote-a/remote-a.bin"
DEFAULT_SOCKET = pathlib.Path.home() / ".signalk/plugin-config-data/semantic-boat-devices-rs485/control.sock"


def request(socket_path, message):
    """Sends one request and yields the reply lines (dicts) until done or error."""
    try:
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        connection.connect(str(socket_path))
    except OSError as error:
        sys.exit(f"cannot reach the RS485 plugin at {socket_path}: {error}\n"
                 "Is Signal K running with 'Semantic Boat Devices RS485' enabled?")
    with connection:
        connection.sendall((json.dumps(message) + "\n").encode())
        buffer = b""
        while True:
            chunk = connection.recv(4096)
            if not chunk:
                yield {"event": "error", "message": "the plugin closed the connection"}
                return
            buffer += chunk
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                reply = json.loads(line)
                yield reply
                if reply.get("event") in ("done", "error"):
                    return


def single(socket_path, message):
    reply = list(request(socket_path, message))[-1]
    if reply.get("event") == "error":
        sys.exit(f"error: {reply.get('message')}")
    return reply


def describe(status):
    mode = {1: "application", 2: "bootloader"}.get(status.get("mode"), "unknown")
    extra = ", not yet confirmed" if status.get("unconfirmed") else ""
    return f"{mode}: {status.get('text')} (image CRC {status.get('imageCrc', 0):08X}{extra})"


def update(socket_path, device, image):
    print(f"{device}: updating with {image}", flush=True)
    for reply in request(socket_path, {"command": "firmware-update", "device": device, "image": str(image)}):
        event = reply.pop("event")
        if event == "progress":
            phase = reply.pop("phase")
            if phase == "sending":
                print(f"{device}:   sending {reply['percent']:3d} % ({reply['bytes']} of {reply['of']} bytes)", flush=True)
            else:
                details = ", ".join(f"{key} {value}" for key, value in reply.items())
                print(f"{device}:   {phase}" + (f" ({details})" if details else ""), flush=True)
        elif event == "done":
            print(f"{device}: done in {reply['seconds']} s, running {reply['version']}", flush=True)
            return True
        else:
            print(f"{device}: FAILED: {reply.get('message')}", flush=True)
            return False
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--socket", default=os.environ.get("REMOTE_A_CONTROL_SOCKET", DEFAULT_SOCKET),
                        help="control socket of the RS485 plugin")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="displays known to the plugin")
    commands.add_parser("bus", help="RS485 bus counters")
    status = commands.add_parser("status", help="running firmware of a display")
    status.add_argument("device")
    run = commands.add_parser("update", help="update displays ('all': every display online)")
    run.add_argument("devices", nargs="+")
    run.add_argument("--image", default=DEFAULT_IMAGE, type=pathlib.Path)
    arguments = parser.parse_args()
    socket_path = pathlib.Path(arguments.socket)

    if arguments.command == "list":
        for device in single(socket_path, {"command": "devices"})["devices"]:
            state = "online" if device["online"] else "offline"
            print(f"{device['id']:16} {device['address']:20} {state:8} {device['label']}")
    elif arguments.command == "bus":
        print(json.dumps(single(socket_path, {"command": "bus"})["buses"], indent=2))
    elif arguments.command == "status":
        print(f"{arguments.device}: {describe(single(socket_path, {'command': 'firmware-status', 'device': arguments.device})['status'])}")
    else:
        image = arguments.image.resolve()
        if not image.is_file():
            sys.exit(f"no image at {image}: build it with script/build.sh remote-a-rs485 release")
        devices = arguments.devices
        if devices == ["all"]:
            devices = [device["id"] for device in single(socket_path, {"command": "devices"})["devices"] if device["online"]]
            if not devices:
                sys.exit("no display is online")
        results = [update(socket_path, device, image) for device in devices]
        sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
