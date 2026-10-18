"""Bench CAN / NMEA 2000 monitor for can-mcu firmware.

Prints every frame on the bus, decodes NMEA 2000 identifiers, reassembles
fast packets, and decodes the PGNs used by the can-mcu firmware (address
claim, ISO request/acknowledgement, heartbeat, product information, PGN list,
temperature, humidity) and the B1S messages described in
src/lib/b1s/registry.yaml. It can also act on the bus from its own source
address:

    python can_bench.py                              # listen, auto-detect adapter
    python can_bench.py --duration 20 --request 126996 --request 126464
    python can_bench.py --duration 20 --conflict     # address contention tests
    python can_bench.py --interface socketcan --channel can0

At start the bench sends a global request for address claims to discover
the nodes. Requests go to the node that claimed an address most recently
(or --dest).
"""

import argparse
import pathlib
import struct
import sys
import time

import can
import yaml

BENCH_ADDRESS = 0x99
FAST_PACKET_PGNS = {126464, 126996, 126998, 129029, 129540}

# B1S, decoded from its registry (src/lib/b1s/README.md).
B1S_REGISTRY = pathlib.Path(__file__).resolve().parents[2] / "lib" / "b1s" / "registry.yaml"
with B1S_REGISTRY.open() as registry_file:
    B1S = yaml.safe_load(registry_file)
B1S_PGNS = {entry["pgn"]: entry["id"] for entry in B1S["pgns"]}
B1S_QUANTITIES = {entry["code"]: entry for entry in B1S["quantities"]}
B1S_ENCODINGS = {0: "b1s", 1: "standard", 2: "off", 3: "n/a"}
FAST_PACKET_PGNS |= {entry["pgn"] for entry in B1S["pgns"] if entry["kind"] == "fast"}

PGN_NAMES = {
    59392: "ISO Acknowledgement",
    59904: "ISO Request",
    60928: "ISO Address Claim",
    126464: "PGN List",
    126993: "Heartbeat",
    126996: "Product Information",
    127501: "Binary Switch Bank Status",
    127508: "Battery Status",
    130312: "Temperature",
    130313: "Humidity",
    130316: "Temperature, Extended Range",
}
PGN_NAMES.update({pgn: f"B1S {name}" for pgn, name in B1S_PGNS.items()})

TEMPERATURE_SOURCES = {0: "sea", 1: "outside", 2: "inside", 3: "engine room", 4: "main cabin",
                       7: "refrigeration", 8: "heating", 13: "freezer", 14: "exhaust"}


# --- identifiers -----------------------------------------------------------

def decode_id(raw):
    priority = (raw >> 26) & 7
    source = raw & 0xFF
    pgn_field = (raw >> 8) & 0x3FFFF
    if ((pgn_field >> 8) & 0xFF) < 240:
        return priority, pgn_field & 0x3FF00, source, pgn_field & 0xFF
    return priority, pgn_field, source, 255


def encode_id(priority, pgn, source, destination=255):
    field = pgn & 0x3FFFF
    if ((field >> 8) & 0xFF) < 240:
        field = (field & 0x3FF00) | destination
    return (priority & 7) << 26 | field << 8 | source


def name_value(unique, manufacturer, function, device_class, instance=0, system=0, group=4, arbitrary=1):
    return (unique & 0x1FFFFF | (manufacturer & 0x7FF) << 21 | (instance & 0xFF) << 32 |
            function << 40 | (device_class & 0x7F) << 49 | (system & 0xF) << 56 |
            (group & 7) << 60 | arbitrary << 63)


def describe_name(value):
    return (f"unique {value & 0x1FFFFF}, mfr {(value >> 21) & 0x7FF}, function {(value >> 40) & 0xFF}, "
            f"class {(value >> 49) & 0x7F}, group {(value >> 60) & 7}, NAME 0x{value:016X}")


# --- payload decoders ------------------------------------------------------

def text(field):
    return field.rstrip(b"\xff\x00 @").decode("ascii", "replace")


def b1s_value(quantity, raw):
    special = {0x7FFFFFFF: "not available", 0x7FFFFFFE: "error", 0x7FFFFFFD: "out of range"}
    if raw in special:
        return special[raw]
    entry = B1S_QUANTITIES.get(quantity)
    if entry is None:
        return f"raw {raw} (unknown quantity)"
    value = raw * entry["scale"]
    text = f"{value:.6g} {entry['unit']}"
    if entry["unit"] == "K":
        text += f" ({value - 273.15:.3f} C)"
    elif entry["id"] == "relativeHumidity":
        text += f" ({value * 100:.2f} %)"
    elif entry["id"] == "co2":
        text += f" ({value * 1e6:.1f} ppm)"
    return text


def lau(data, offset):
    """NMEA 2000 STRING_LAU at offset: returns (text, next offset)."""
    length = data[offset]
    return data[offset + 2:offset + length].decode("ascii", "replace"), offset + length


def decode_b1s(pgn, data):
    if len(data) < 2 or data[0] | data[1] << 8 != B1S["proprietary_header"]:
        manufacturer = (data[0] | (data[1] & 7) << 8) if len(data) >= 2 else "?"
        return f"proprietary, manufacturer {manufacturer} (not B1S)"
    name = B1S_PGNS[pgn]
    if name == "measurement" and len(data) >= 8:
        quantity, channel = data[2], data[3]
        raw = struct.unpack_from("<i", data, 4)[0]
        label = B1S_QUANTITIES.get(quantity, {}).get("id", f"quantity {quantity}")
        return f"channel {channel}, {label} = {b1s_value(quantity, raw)}"
    if name == "channelDescription" and len(data) >= 12:
        channel, quantity, flags, instance = data[2], data[3], data[4], data[5]
        period = struct.unpack_from("<H", data, 6)[0]
        location, offset = lau(data, 8)
        label, _ = lau(data, offset)
        kind = B1S_QUANTITIES.get(quantity, {}).get("id", f"quantity {quantity}")
        if flags & 0x04:
            kind = f"output kind {quantity}"
        standard = f", standard instance {instance}" if instance != 0xFF else ""
        every = "on change" if period == 0xFFFF else f"every {period / 100:g} s"
        encoding = B1S_ENCODINGS[flags & 3] + (" (no standard PGN)" if flags & 0x08 else "")
        return (f"channel {channel}: {kind}, sent as {encoding}{standard}, {every}, "
                f"location '{location or '(unassigned)'}', label '{label}'")
    return f"{name}: {data[2:].hex(' ')}"


def decode_payload(pgn, data):
    if pgn in B1S_PGNS:
        return decode_b1s(pgn, data)
    if pgn == 60928 and len(data) >= 8:
        return describe_name(struct.unpack_from("<Q", data)[0])
    if pgn == 59904 and len(data) >= 3:
        requested = int.from_bytes(data[:3], "little")
        return f"request PGN {requested} ({PGN_NAMES.get(requested, '?')})"
    if pgn == 59392 and len(data) >= 8:
        control = {0: "ACK", 1: "NACK", 2: "access denied", 3: "cannot respond"}.get(data[0], data[0])
        return f"{control} for PGN {int.from_bytes(data[5:8], 'little')}"
    if pgn == 126993 and len(data) >= 4:
        interval, sequence = struct.unpack_from("<HB", data)
        states = {0: "error-active", 1: "error-passive", 2: "bus-off", 3: "n/a"}
        return (f"interval {interval / 100:.2f} s, seq {sequence}, controller {states[data[3] & 3]}, "
                f"equipment {'operational' if ((data[3] >> 4) & 3) == 0 else (data[3] >> 4) & 3}")
    if pgn == 126996 and len(data) >= 134:
        version, code = struct.unpack_from("<HH", data)
        return (f"N2K {version / 1000:.3f}, product {code}, model '{text(data[4:36])}', "
                f"software '{text(data[36:68])}', version '{text(data[68:100])}', "
                f"serial '{text(data[100:132])}', cert {data[132]}, LEN {data[133]}")
    if pgn == 126464 and len(data) >= 1:
        pgns = [int.from_bytes(data[i:i + 3], "little") for i in range(1, len(data) - 2, 3)]
        return f"{'transmit' if data[0] == 0 else 'receive'} PGNs {pgns}"
    if pgn == 127508 and len(data) >= 8:
        voltage = struct.unpack_from("<h", data, 1)[0]
        return f"instance {data[0]}, {voltage / 100:.2f} V"
    if pgn == 127501 and len(data) >= 2:
        states = []
        for index in range(28):
            value = (data[1 + index // 4] >> (2 * (index % 4))) & 3
            if value != 3:
                states.append("1" if value == 1 else "0")
        return f"bank {data[0]}, states {''.join(states)}"
    if pgn == 130313 and len(data) >= 5:
        humidity = struct.unpack_from("<h", data, 3)[0] * 0.004
        source = {0: "inside", 1: "outside"}.get(data[2], data[2])
        return f"instance {data[1]}, source {source}, {humidity:.2f} %"
    if pgn == 130316 and len(data) >= 8:
        kelvin = int.from_bytes(data[3:6], "little") / 1000.0
        source = TEMPERATURE_SOURCES.get(data[2], data[2])
        return f"instance {data[1]}, source {source}, {kelvin - 273.15:.2f} C"
    return None


# --- fast packets ------------------------------------------------------------

class FastPacketAssembler:
    def __init__(self):
        self.pending = {}

    def add(self, key, data):
        """Returns the complete payload, or None while frames are missing."""
        sequence, frame = data[0] >> 5, data[0] & 0x1F
        if frame == 0:
            self.pending[key] = {"sequence": sequence, "length": data[1], "next": 1, "data": bytearray(data[2:])}
        else:
            entry = self.pending.get(key)
            if entry is None or entry["sequence"] != sequence or entry["next"] != frame:
                self.pending.pop(key, None)
                return None
            entry["data"] += data[1:]
            entry["next"] += 1
        entry = self.pending[key]
        if len(entry["data"]) >= entry["length"]:
            del self.pending[key]
            return bytes(entry["data"][:entry["length"]])
        return None


# --- bench -------------------------------------------------------------------

def open_bus(args):
    channel = args.channel
    if channel is None and args.interface == "candle":
        configs = can.detect_available_configs(interfaces=["candle"])
        if not configs:
            sys.exit("No candle/gs_usb adapter found.")
        channel = configs[0]["channel"]
    kwargs = {"interface": args.interface, "channel": channel, "bitrate": args.bitrate}
    if args.interface == "candle":
        # python-can's timing search finds no exact 87.5 % solution for the
        # adapter's 160 MHz clock at 250 kbit/s; 85 % is well within tolerance
        # on a short bench bus.
        kwargs["fd"] = False
        kwargs["sample_point"] = args.sample_point
    bus = can.Bus(**kwargs)
    print(f"[can-bench] {args.interface} {channel} at {args.bitrate} bit/s, bench address {BENCH_ADDRESS}",
          flush=True)
    return bus


class Bench:
    def __init__(self, args, bus):
        self.args = args
        self.bus = bus
        self.started = time.monotonic()
        self.assembler = FastPacketAssembler()
        self.claims = {}          # address -> NAME
        self.last_claimer = None  # (address, NAME) of the most recent claim
        self.frames = 0
        self.messages = 0
        self.send_failures = 0
        self.actions = []
        self.next_stats = None
        self.window = {"frames": 0, "errors": 0, "pgns": {}, "sources": {}}
        self.heartbeats = {}      # source -> last sequence counter
        self.heartbeat_gaps = 0

    def elapsed(self):
        return time.monotonic() - self.started

    def send(self, priority, pgn, destination, data, label, source=BENCH_ADDRESS):
        message = can.Message(arbitration_id=encode_id(priority, pgn, source, destination),
                              is_extended_id=True, data=data)
        try:
            self.bus.send(message, timeout=0.2)
            print(f"{self.elapsed():9.3f}  >> {label}", flush=True)
        except can.CanError as error:
            self.send_failures += 1
            print(f"{self.elapsed():9.3f}  >> {label} FAILED: {error}", flush=True)

    def target(self):
        if self.args.dest is not None:
            return self.args.dest
        return self.last_claimer[0] if self.last_claimer else 255

    def request(self, pgn, destination=None):
        destination = self.target() if destination is None else destination
        self.send(6, 59904, destination, pgn.to_bytes(3, "little"),
                  f"ISO request PGN {pgn} ({PGN_NAMES.get(pgn, '?')}) to {destination}")

    def contend(self, lower):
        if not self.last_claimer:
            print(f"{self.elapsed():9.3f}  !! no claimed node seen, contention test skipped", flush=True)
            return
        address, theirs = self.last_claimer
        ours = 0 if lower else (1 << 64) - 1
        verdict = "lower NAME, node must move" if lower else "higher NAME, node must re-assert"
        # A claim competes for the address it is sent from.
        self.send(6, 60928, 255, struct.pack("<Q", ours),
                  f"address claim for {address} with {verdict}", source=address)

    def schedule(self):
        # Discover the nodes on the bus: every node answers a global request
        # for the address claim with its claim.
        self.actions.append((0.2, lambda: self.request(60928, destination=255)))
        at = 1.0
        for pgn in self.args.request:
            self.actions.append((at, lambda pgn=pgn: self.request(pgn)))
            at += 1.0
        if self.args.request_unsupported:
            self.actions.append((at, lambda: self.request(65000)))
            at += 1.0
        if self.args.conflict:
            self.actions.append((at + 1.0, lambda: self.contend(lower=False)))
            self.actions.append((at + 3.0, lambda: self.contend(lower=True)))
        self.actions.sort(key=lambda item: item[0])

    def stats(self):
        window = self.window
        seconds = self.args.stats
        pgns = ", ".join(f"{pgn}:{count}" for pgn, count in sorted(window["pgns"].items()))
        sources = ", ".join(f"{source}:{count}" for source, count in sorted(window["sources"].items()))
        print(f"{self.elapsed():9.3f}  STATS {window['frames']} frames ({window['frames'] / seconds:.1f}/s), "
              f"{window['errors']} error frames, heartbeat gaps {self.heartbeat_gaps}; "
              f"messages per PGN {{{pgns}}}; per source {{{sources}}}", flush=True)
        self.window = {"frames": 0, "errors": 0, "pgns": {}, "sources": {}}

    def handle(self, message):
        self.frames += 1
        self.window["frames"] += 1
        now = self.elapsed()
        if message.is_error_frame:
            self.window["errors"] += 1
            print(f"{now:9.3f}  ERROR FRAME", flush=True)
            return
        if not message.is_extended_id:
            print(f"{now:9.3f}  std {message.arbitration_id:03X} [{message.dlc}] {message.data.hex(' ')}", flush=True)
            return
        priority, pgn, source, destination = decode_id(message.arbitration_id)
        data = bytes(message.data)
        if pgn in FAST_PACKET_PGNS:
            data = self.assembler.add((source, pgn), data)
            if data is None:
                return
        self.messages += 1
        self.window["pgns"][pgn] = self.window["pgns"].get(pgn, 0) + 1
        self.window["sources"][source] = self.window["sources"].get(source, 0) + 1
        if pgn == 126993 and len(data) >= 3:
            sequence = data[2]
            last = self.heartbeats.get(source)
            if last is not None and sequence != (last + 1) % 253:
                self.heartbeat_gaps += 1
                print(f"{now:9.3f}  !! heartbeat sequence from {source}: {last} -> {sequence} "
                      f"(restart or lost heartbeats)", flush=True)
            self.heartbeats[source] = sequence
        if pgn == 60928 and len(data) >= 8:
            name = struct.unpack_from("<Q", data)[0]
            if source != BENCH_ADDRESS and source != 254 and name not in (0, (1 << 64) - 1):
                self.claims[source] = name
                self.last_claimer = (source, name)
        if self.args.quiet and pgn not in (60928, 59392):
            return
        decoded = decode_payload(pgn, data)
        target = "" if destination == 255 else f" -> {destination}"
        head = f"{now:9.3f}  PGN {pgn:6d} {PGN_NAMES.get(pgn, ''):<28} src {source:3d}{target} p{priority}"
        print(f"{head}  {decoded if decoded is not None else data.hex(' ')}", flush=True)

    def loop(self):
        self.schedule()
        if self.args.stats > 0:
            self.next_stats = self.args.stats
        while self.args.duration <= 0 or self.elapsed() < self.args.duration:
            if self.next_stats is not None and self.elapsed() >= self.next_stats:
                self.next_stats += self.args.stats
                self.stats()
            while self.actions and self.elapsed() >= self.actions[0][0]:
                self.actions.pop(0)[1]()
            message = self.bus.recv(timeout=0.05)
            if message is not None:
                self.handle(message)

    def summary(self):
        nodes = ", ".join(f"{address}: {describe_name(name)}" for address, name in sorted(self.claims.items()))
        print(f"[can-bench] {self.elapsed():.1f} s: {self.frames} frame(s), {self.messages} message(s), "
              f"send failures {self.send_failures}", flush=True)
        print(f"[can-bench] claimed addresses: {nodes or 'none'}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--interface", default="candle")
    parser.add_argument("--channel", default=None)
    parser.add_argument("--bitrate", type=int, default=250000)
    parser.add_argument("--sample-point", type=float, default=85.0, help="candle only, percent")
    parser.add_argument("--duration", type=float, default=0.0, help="seconds to run; 0 = until Ctrl+C")
    parser.add_argument("--request", type=int, action="append", default=[], metavar="PGN",
                        help="send an ISO request for PGN (repeatable), one per second from t=1 s")
    parser.add_argument("--request-unsupported", action="store_true",
                        help="also request PGN 65000, which must be NACKed")
    parser.add_argument("--dest", type=int, default=None, help="request destination (default: last claimer)")
    parser.add_argument("--conflict", action="store_true",
                        help="contend for the claimed address with a higher, then a lower NAME")
    parser.add_argument("--quiet", action="store_true", help="only print address claims and acknowledgements")
    parser.add_argument("--stats", type=float, default=0.0, metavar="SECONDS",
                        help="print traffic statistics every SECONDS")
    args = parser.parse_args()

    bus = open_bus(args)
    bench = Bench(args, bus)
    try:
        bench.loop()
    except KeyboardInterrupt:
        pass
    finally:
        bus.shutdown()
        bench.summary()


if __name__ == "__main__":
    main()
