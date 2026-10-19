"""B1S configuration tool: parameters, commands, and the console of B1S devices.

Configuration over CAN as specified in src/lib/b1s/CONFIGURATION.md, from the
bench address (153). Values are raw B1S values (SI unit x quantity scale);
`params` also shows them in SI units.

    python b1s_tool.py devices
    python b1s_tool.py params 38
    python b1s_tool.py get 38 channelLocation 2
    python b1s_tool.py set 38 channelLocation 2 inside
    python b1s_tool.py set 38 alarm1 0 function=average window=10 comparison=above threshold=323150
    python b1s_tool.py command 38 calibrate 2 295150      # air channel to 22.0 C
    python b1s_tool.py console 38 "sensor air"
    python b1s_tool.py console 38 --follow 20             # live device log for 20 s
    python b1s_tool.py firmware 38 lab-test-01.bin         # update over CAN (FIRMWARE.md)
    python b1s_tool.py log 38 5                            # stream the device log for 5 minutes
    python b1s_tool.py log 38 0                            # stop it
"""

import argparse
import struct
import sys
import time

import can

from can_bench import B1S, B1S_QUANTITIES, BENCH_ADDRESS, FastPacketAssembler, decode_id, encode_id

HEADER = struct.pack("<H", B1S["proprietary_header"])
PGN = {entry["id"]: entry["pgn"] for entry in B1S["pgns"]}
SERVICES = {entry["id"]: entry["code"] for entry in B1S["services"]}
COMMANDS = {entry["id"]: entry["code"] for entry in B1S["commands"]}
PARAMETERS = {entry["id"]: entry for entry in B1S["parameters"]}
PARAMETER_IDS = {entry["code"]: entry for entry in B1S["parameters"]}
PARAMETER_STATUS = ["done", "unknown parameter", "unknown channel", "invalid value", "read-only", "busy",
                    "not permitted", "no resources"]
RECORDS = B1S.get("records", {})
FIELD_FORMATS = {"uint8": "B", "enum": "B", "flags": "B", "uint16": "H", "int32": "i"}


def decode_record(name, data):
    """Record bytes -> {field: value}; enums as names, flags as lists of names."""
    fields, offset = {}, 0
    for field in RECORDS[name]:
        fmt = FIELD_FORMATS[field["type"]]
        value = struct.unpack_from("<" + fmt, data, offset)[0]
        offset += struct.calcsize(fmt)
        if field["type"] == "enum":
            values = field["values"]
            value = values[value] if value < len(values) else value
        elif field["type"] == "flags":
            value = [bit for index, bit in enumerate(field["bits"]) if value >> index & 1]
        fields[field["name"]] = value
    return fields


def encode_record(name, fields):
    data = b""
    for field in RECORDS[name]:
        value = fields[field["name"]]
        if field["type"] == "enum" and isinstance(value, str):
            value = field["values"].index(value)
        elif field["type"] == "flags":
            value = sum(1 << field["bits"].index(bit) for bit in value)
        data += struct.pack("<" + FIELD_FORMATS[field["type"]], int(value))
    return data
COMMAND_RESULTS = ["done", "unknown command", "unknown channel", "invalid argument", "refused", "busy"]
REPLY = 0x80


class Client:
    def __init__(self, channel, timeout=1.0):
        self.bus = can.Bus(interface="socketcan", channel=channel)
        self.assembler = FastPacketAssembler()
        self.sequence = 0
        self.fast_sequence = 0
        self.timeout = timeout

    def close(self):
        self.bus.shutdown()

    def transmit(self, message):
        """Sends a frame; waits while the interface queue is full (SocketCAN
        reports ENOBUFS instead of blocking; can1 queues only 10 frames)."""
        deadline = time.monotonic() + 1.0
        while True:
            try:
                self.bus.send(message, timeout=1.0)
                return
            except can.CanOperationError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.002)

    def send_single(self, priority, pgn, destination, data):
        self.transmit(can.Message(arbitration_id=encode_id(priority, pgn, BENCH_ADDRESS, destination),
                                  is_extended_id=True, data=bytes(data).ljust(8, b"\xff")))

    def send_fast(self, priority, pgn, destination, data):
        self.fast_sequence = (self.fast_sequence + 1) & 7
        payload = bytes(data)
        frames = [bytes([self.fast_sequence << 5, len(payload)]) + payload[:6]]
        offset, index = 6, 1
        while offset < len(payload):
            frames.append(bytes([self.fast_sequence << 5 | index]) + payload[offset:offset + 7])
            offset, index = offset + 7, index + 1
        for frame in frames:
            self.transmit(can.Message(arbitration_id=encode_id(priority, pgn, BENCH_ADDRESS, destination),
                                      is_extended_id=True, data=frame.ljust(8, b"\xff")))

    def receive(self, until, accept):
        """Messages (pgn, source, payload) until `accept` returns a result or time runs out."""
        while time.monotonic() < until:
            message = self.bus.recv(timeout=0.05)
            if message is None or not message.is_extended_id:
                continue
            _, pgn, source, destination = decode_id(message.arbitration_id)
            data = bytes(message.data)
            if pgn in (PGN["service"], PGN["channelDescription"]):
                data = self.assembler.add((source, pgn), data)
                if data is None:
                    continue
            result = accept(pgn, source, destination, data)
            if result is not None:
                return result
        return None

    def service(self, address, service, fields=b""):
        """One request and its reply payload (after the header), with three tries."""
        for _ in range(3):
            self.sequence = (self.sequence + 1) & 0xFF
            sequence = self.sequence
            self.send_fast(6, PGN["service"], address, HEADER + bytes([SERVICES[service], sequence]) + fields)

            def accept(pgn, source, destination, data):
                if (pgn == PGN["service"] and source == address and destination == BENCH_ADDRESS and
                        data[:2] == HEADER and data[2] == SERVICES[service] | REPLY and data[3] == sequence):
                    return data[4:]
                return None
            reply = self.receive(time.monotonic() + self.timeout, accept)
            if reply is not None:
                return reply
        sys.exit(f"no reply from {address} to {service}")

    def command(self, address, command, channel, argument):
        payload = HEADER + bytes([COMMANDS[command], channel]) + struct.pack("<i", argument)
        self.send_single(3, PGN["command"], address, payload)

        def accept(pgn, source, destination, data):
            if (pgn == PGN["command"] and source == address and destination == BENCH_ADDRESS and
                    data[:2] == HEADER and data[2] == COMMANDS[command] | REPLY):
                return data[4]
            return None
        result = self.receive(time.monotonic() + self.timeout, accept)
        if result is None:
            sys.exit(f"no reply from {address} to {command}")
        return COMMAND_RESULTS[result] if result < len(COMMAND_RESULTS) else f"result {result}"

    def parameter_ids(self, address):
        ids, first = [], 0
        while True:
            reply = self.service(address, "parameterList", bytes([first]))
            count, more = reply[2], reply[3]
            ids += [struct.unpack_from("<H", reply, 4 + 2 * index)[0] for index in range(count)]
            if not more:
                return ids
            first += count

    def read(self, address, parameter, channel=0xFF):
        reply = self.service(address, "parameterRead", struct.pack("<HB", parameter, channel))
        return decode_value(reply)

    def write(self, address, parameter, channel, value):
        if isinstance(value, bytes):
            encoded = bytes([2, len(value)]) + value
        elif isinstance(value, str):
            text = value.encode("ascii")
            encoded = bytes([1, len(text) + 2, 1]) + text
        else:
            encoded = bytes([0]) + struct.pack("<i", value)
        reply = self.service(address, "parameterWrite", struct.pack("<HB", parameter, channel) + encoded)
        return decode_value(reply)

    def claims(self, seconds=1.0):
        """Address claims after a global request: address -> NAME."""
        self.send_single(6, 59904, 255, (60928).to_bytes(3, "little"))
        seen = {}

        def accept(pgn, source, destination, data):
            if pgn == 60928 and len(data) >= 8:
                seen[source] = struct.unpack_from("<Q", data)[0]
            return None
        self.receive(time.monotonic() + seconds, accept)
        return seen

    def find(self, unique, bootloader, seconds):
        """Address of the device with this unique number, in the bootloader
        (device class 10) or not, within `seconds`."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            for address, name in self.claims().items():
                if name & 0x1FFFFF == unique and (((name >> 49) & 0x7F) == 10) == bootloader:
                    return address
        return None

    def update(self, address, image):
        """Firmware update over CAN (src/lib/b1s/FIRMWARE.md)."""
        header = image[0x200:0x240]
        magic, _, board, product, _, size, crc = struct.unpack_from("<IHHHHII", header)
        text = header[24:].split(b"\0")[0].decode("ascii", "replace")
        if magic != 0x21533142 or size != len(image):
            sys.exit("not a stamped B1S image (stamp_image.py)")
        print(f"image: {size} bytes, CRC {crc:08X}, board {board}, product {product}, {text}")
        names = self.claims()
        if address not in names:
            sys.exit(f"no device at address {address}")
        unique = names[address] & 0x1FFFFF
        bootloader = address if ((names[address] >> 49) & 0x7F) == 10 else None
        started = time.monotonic()
        if bootloader is None:
            print(f"device {unique} at {address}: enterBootloader -> {self.command(address, 'enterBootloader', 0xFF, -1)}")
            bootloader = self.find(unique, True, 10.0)
            if bootloader is None:
                sys.exit("the device did not appear in its bootloader")
        print(f"bootloader of device {unique} at address {bootloader}")
        reply = self.service(bootloader, "firmwareBegin", header)
        if reply[0] != 0:
            sys.exit(f"firmwareBegin refused: status {reply[0]}")
        chunk = reply[1]
        offset = 0
        last_report = 0
        while offset < size:
            data = image[offset:offset + chunk]
            reply = self.service(bootloader, "firmwareData", struct.pack("<I", offset) + data)
            status, following = reply[0], struct.unpack_from("<I", reply, 1)[0]
            if status == 10:  # unexpected offset: continue where the device is
                offset = following
                continue
            if status != 0:
                sys.exit(f"firmwareData at {offset} refused: status {status}")
            offset = following
            if offset - last_report >= 16384 or offset == size:
                last_report = offset
                elapsed = time.monotonic() - started
                print(f"  {offset:7d} / {size} bytes, {offset / 1024 / max(elapsed, 0.001):.1f} KB/s")
        reply = self.service(bootloader, "firmwareEnd")
        if reply[0] != 0:
            sys.exit(f"firmwareEnd refused: status {reply[0]} (11 = CRC mismatch)")
        print(f"image written and checked in {time.monotonic() - started:.1f} s; the device restarts")
        application = self.find(unique, False, 15.0)
        if application is None:
            sys.exit("the application did not come back (the bootloader keeps waiting for an image)")
        print(f"application back at address {application}; waiting for it to confirm itself")
        # Applications confirm a new image after 10 s of normal running: an
        # uptime above that (without a restart in between) means it did.
        time.sleep(12.0)
        application = self.find(unique, False, 3.0)
        if application is None:
            sys.exit("the application did not stay up")
        status, uptime = self.read(application, PARAMETERS["uptime"]["code"])
        if status != "done" or uptime < 10:
            sys.exit(f"the application restarted (uptime {uptime} s): not confirmed, the bootloader takes "
                     "over after three starts")
        print(f"update complete in {time.monotonic() - started:.1f} s; the application runs ({uptime} s) "
              "and has confirmed the image")

    def log_stream(self, address, minutes):
        """Starts (minutes 1..60), stops (0), or asks (255) for the log
        stream; returns the remaining seconds."""
        reply = self.service(address, "logStream", bytes([minutes]))
        return struct.unpack_from("<H", reply, 1)[0] if reply[0] == 0 else None

    def channels(self, address):
        """Channel numbers and quantities from the device's Channel Descriptions."""
        self.send_single(6, 59904, address, PGN["channelDescription"].to_bytes(3, "little"))
        found = {}

        def accept(pgn, source, destination, data):
            if pgn == PGN["channelDescription"] and source == address and data[:2] == HEADER:
                found[data[2]] = data[3]
            return None
        self.receive(time.monotonic() + 1.0, accept)
        return found


def decode_value(reply):
    status = reply[0]
    if status != 0:
        return PARAMETER_STATUS[status] if status < len(PARAMETER_STATUS) else f"status {status}", None
    kind = reply[4]
    if kind == 1:
        length = reply[5]
        return "done", reply[7:5 + length].decode("ascii", "replace")
    if kind == 2:
        return "done", bytes(reply[6:6 + reply[5]])
    return "done", struct.unpack_from("<i", reply, 5)[0]


def si_text(value, unit, quantity, absolute):
    q = B1S_QUANTITIES.get(quantity)
    if not q or unit not in ("quantity", "quantityDelta", "alarmValue", "alarmDelta"):
        return str(value)
    si = value * q["scale"]
    if absolute and q["unit"] == "K":
        return f"{value} ({si - 273.15:.3f} C)"
    return f"{value} ({si:.6g} {q['unit']})"


def show(entry, value, quantity):
    if isinstance(value, bytes) and entry.get("record") in RECORDS:
        fields = decode_record(entry["record"], value)
        if fields.get("function") == "none":
            return "unused"
        absolute = fields.get("function") not in ("change", "rate")
        parts = []
        for field in RECORDS[entry["record"]]:
            name, unit = field["name"], field.get("unit")
            shown = fields[name]
            if isinstance(shown, int):
                shown = si_text(shown, unit, quantity, absolute and unit == "alarmValue")
            elif isinstance(shown, list):
                shown = ",".join(shown) or "-"
            parts.append(f"{name}={shown}")
        return " ".join(parts)
    if not isinstance(value, int):
        return repr(value)
    unit = entry.get("unit")
    if entry.get("type") == "enum":
        values = entry.get("values", [])
        return values[value] if 0 <= value < len(values) else str(value)
    if unit in ("quantity", "quantityDelta"):
        return si_text(value, unit, quantity, unit == "quantity")
    if entry["id"] == "alarmState":
        names = [f"rule {slot}" if slot < 4 else "fault" for slot in range(5)]
        states = [f"{names[slot]} {'acknowledged' if value >> (2 * slot + 1) & 1 else 'ACTIVE'}"
                  for slot in range(5) if value >> (2 * slot) & 1]
        return ", ".join(states) or "no active alarm"
    return f"{value}{' ' + unit if unit and unit not in ('quantity',) else ''}"


def parameter_code(name):
    if name not in PARAMETERS:
        sys.exit(f"unknown parameter {name}; known: {', '.join(PARAMETERS)}")
    return PARAMETERS[name]["code"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--can", default="can1", help="SocketCAN interface of B1S")
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("devices")
    params = sub.add_parser("params")
    params.add_argument("address", type=int)
    get = sub.add_parser("get")
    get.add_argument("address", type=int)
    get.add_argument("parameter")
    get.add_argument("channel", type=int, nargs="?", default=0xFF)
    put = sub.add_parser("set")
    put.add_argument("address", type=int)
    put.add_argument("parameter")
    put.add_argument("rest", nargs="+", help="[channel] value")
    command = sub.add_parser("command")
    command.add_argument("address", type=int)
    command.add_argument("command", choices=list(COMMANDS))
    command.add_argument("channel", type=int, nargs="?", default=0xFF)
    command.add_argument("argument", type=int, nargs="?", default=-1)
    console = sub.add_parser("console")
    console.add_argument("address", type=int)
    console.add_argument("line", nargs="?", default="")
    console.add_argument("--follow", type=float, default=2.0, metavar="SECONDS", help="how long to show the log")
    log = sub.add_parser("log", help="stream the device log (rate limited, stops by itself)")
    log.add_argument("address", type=int)
    log.add_argument("minutes", type=int, help="1..60 start or extend, 0 stop, 255 only ask")
    log.add_argument("--follow", type=float, default=None, metavar="SECONDS",
                     help="how long to show the log (default: while it streams)")
    firmware = sub.add_parser("firmware", help="update the firmware over CAN")
    firmware.add_argument("address", type=int, help="the device's address (application or bootloader)")
    firmware.add_argument("image", type=argparse.FileType("rb"), help="stamped image (.bin)")
    args = parser.parse_args()

    client = Client(args.can)
    try:
        if args.action == "devices":
            client.send_single(6, 59904, 255, (60928).to_bytes(3, "little"))
            seen = {}

            def accept(pgn, source, destination, data):
                if pgn == 60928 and len(data) >= 8:
                    name = struct.unpack_from("<Q", data)[0]
                    seen[source] = name
                return None
            client.receive(time.monotonic() + 1.0, accept)
            for address, name in sorted(seen.items()):
                maker = (name >> 21) & 0x7FF
                tag = "B1S" if maker == B1S["manufacturer_code"] else f"manufacturer {maker}"
                print(f"{address:3d}  device ID {name & 0x1FFFFF:8d}  {tag}")
        elif args.action == "params":
            ids = client.parameter_ids(args.address)
            channels = client.channels(args.address)
            for code in ids:
                entry = PARAMETER_IDS.get(code, {"id": f"parameter {code}", "scope": "device"})
                targets = sorted(channels) if entry.get("scope") == "channel" else [0xFF]
                for channel in targets:
                    status, value = client.read(args.address, code, channel)
                    where = "" if channel == 0xFF else f"[{channel}]"
                    text = show(entry, value, channels.get(channel)) if status == "done" else f"({status})"
                    print(f"{entry['id'] + where:28s} {text}")
        elif args.action == "get":
            status, value = client.read(args.address, parameter_code(args.parameter), args.channel)
            shown = show(PARAMETERS[args.parameter], value, None) if isinstance(value, bytes) else value
            print(shown if status == "done" else status)
        elif args.action == "set":
            entry = PARAMETERS.get(args.parameter) or sys.exit(f"unknown parameter {args.parameter}")
            channel = int(args.rest[0]) if entry["scope"] == "channel" else 0xFF
            raw = args.rest[1] if entry["scope"] == "channel" else args.rest[0]
            if entry["type"] == "record":
                # field=value pairs (raw values, enum names, flags as a,b) change the current record.
                status, current = client.read(args.address, entry["code"], channel)
                if status != "done":
                    sys.exit(f"read {args.parameter}: {status}")
                fields = decode_record(entry["record"], current)
                pairs = args.rest[1:] if entry["scope"] == "channel" else args.rest
                for pair in pairs:
                    name, _, text = pair.partition("=")
                    if name not in fields:
                        sys.exit(f"{args.parameter} has no field {name}; fields: {', '.join(fields)}")
                    if isinstance(fields[name], list):
                        fields[name] = [bit for bit in text.split(",") if bit]
                    else:
                        fields[name] = text if not text.lstrip("-").isdigit() else int(text)
                value = encode_record(entry["record"], fields)
            elif entry["type"] == "string":
                value = raw
            elif entry["type"] == "enum" and raw in entry.get("values", []):
                value = entry["values"].index(raw)
            else:
                value = int(raw)
            status, stored = client.write(args.address, entry["code"], channel, value)
            print(f"{status}: {show(entry, stored, client.channels(args.address).get(channel))}" if status == "done"
                  else status)
        elif args.action == "command":
            argument = args.argument if args.argument != -1 else -1
            print(client.command(args.address, args.command, args.channel, argument))
        elif args.action == "log":
            remaining = client.log_stream(args.address, args.minutes)
            print(f"[log stream: {remaining} s remaining]", flush=True)
            follow = args.follow if args.follow is not None else (remaining or 0)

            def accept(pgn, source, destination, data):
                if (pgn == PGN["service"] and source == args.address and destination == BENCH_ADDRESS and
                        data[:2] == HEADER and data[2] == SERVICES["console"] | REPLY):
                    sys.stdout.write(data[5:].rstrip(b"\xff\x00").decode("ascii", "replace").replace("\r", ""))
                    sys.stdout.flush()
                return None
            client.receive(time.monotonic() + follow, accept)
        elif args.action == "firmware":
            client.update(args.address, args.image.read())
        elif args.action == "console":
            client.sequence = (client.sequence + 1) & 0xFF
            client.send_fast(6, PGN["service"], args.address,
                             HEADER + bytes([SERVICES["console"], client.sequence]) + args.line.encode("ascii"))

            def accept(pgn, source, destination, data):
                if (pgn == PGN["service"] and source == args.address and data[:2] == HEADER and
                        data[2] == SERVICES["console"] | REPLY):
                    sys.stdout.write(data[5:].decode("ascii", "replace").replace("\r", ""))
                    sys.stdout.flush()
                return None
            client.receive(time.monotonic() + args.follow, accept)
    finally:
        client.close()


if __name__ == "__main__":
    main()
