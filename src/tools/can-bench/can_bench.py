"""Bench CAN monitor for can-mcu firmware.

Prints every frame on the bus, decodes the lab-test-01 raw test frame
(ID 0x18FF0042: counter LE32, uptime s LE16, 0xCA 0xFE) and checks its
sequence, optionally transmits its own test frame once per second
(ID 0x18FF0099, counter LE32), and prints a summary on exit.

Works with any python-can interface; the default is a candleLight/gs_usb
adapter through the `candle` backend on Windows.

    python can_bench.py                      # listen, auto-detect adapter
    python can_bench.py --send --duration 30
    python can_bench.py --interface socketcan --channel can0
"""

import argparse
import struct
import sys
import time

import can

LT1_TEST_ID = 0x18FF0042
BENCH_TEST_ID = 0x18FF0099


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
    print(f"[can-bench] {args.interface} {channel} at {args.bitrate} bit/s", flush=True)
    return bus


def describe(message):
    ident = f"{message.arbitration_id:08X}" if message.is_extended_id else f"{message.arbitration_id:03X}"
    payload = " ".join(f"{byte:02X}" for byte in message.data)
    kind = "ERR" if message.is_error_frame else ("ext" if message.is_extended_id else "std")
    return f"{kind} {ident} [{message.dlc}] {payload}"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--interface", default="candle")
    parser.add_argument("--channel", default=None)
    parser.add_argument("--bitrate", type=int, default=250000)
    parser.add_argument("--sample-point", type=float, default=85.0, help="candle only, percent")
    parser.add_argument("--send", action="store_true", help="transmit a bench test frame every second")
    parser.add_argument("--duration", type=float, default=0.0, help="seconds to run; 0 = until Ctrl+C")
    parser.add_argument("--quiet", action="store_true", help="only print LT1 sequence problems and the summary")
    args = parser.parse_args()

    bus = open_bus(args)
    started = time.monotonic()
    next_send = started
    sent = send_errors = received = error_frames = 0
    lt1_frames = lt1_gaps = 0
    lt1_last = None

    try:
        while args.duration <= 0 or time.monotonic() - started < args.duration:
            now = time.monotonic()
            if args.send and now >= next_send:
                next_send += 1.0
                message = can.Message(arbitration_id=BENCH_TEST_ID, is_extended_id=True,
                                      data=struct.pack("<I", sent) + b"\xBE\x7C\x00\x00")
                try:
                    bus.send(message, timeout=0.2)
                    sent += 1
                except can.CanError as error:
                    send_errors += 1
                    print(f"[can-bench] send failed: {error}", flush=True)

            message = bus.recv(timeout=0.05)
            if message is None:
                continue
            received += 1
            if message.is_error_frame:
                error_frames += 1
            elapsed = message.timestamp - started if message.timestamp > 1e6 else time.monotonic() - started
            line = f"{elapsed:9.3f}  {describe(message)}"
            if message.arbitration_id == LT1_TEST_ID and message.is_extended_id and len(message.data) == 8:
                counter, uptime = struct.unpack_from("<IH", message.data)
                lt1_frames += 1
                note = ""
                if lt1_last is not None and counter != lt1_last + 1:
                    lt1_gaps += 1
                    note = f"  <-- sequence gap (expected {lt1_last + 1})"
                lt1_last = counter
                line += f"   LT1 test #{counter}, uptime {uptime} s{note}"
                if args.quiet and not note:
                    line = None
            elif args.quiet:
                line = None
            if line:
                print(line, flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        bus.shutdown()

    duration = time.monotonic() - started
    print(f"[can-bench] {duration:.1f} s: received {received} frame(s), {error_frames} error frame(s); "
          f"LT1 test frames {lt1_frames}, sequence gaps {lt1_gaps}; "
          f"sent {sent}, send failures {send_errors}", flush=True)


if __name__ == "__main__":
    main()
