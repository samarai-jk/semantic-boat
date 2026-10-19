#!/usr/bin/env python3
"""Generates include/b1s/registry.hpp from registry.yaml.

The header is committed, so firmware builds need neither Python nor PyYAML.
Run after every registry change; --check fails if the header is out of date.

    python3 src/lib/b1s/tools/generate_registry.py
    python3 src/lib/b1s/tools/generate_registry.py --check
"""

import argparse
import pathlib
import re
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "registry.yaml"
HEADER = ROOT / "include" / "b1s" / "registry.hpp"


def identifier(name):
    """lab-test-01 -> labTest01; lowerCamelCase stays as it is."""
    parts = re.split(r"[^A-Za-z0-9]+", name)
    return parts[0] + "".join(part[:1].upper() + part[1:] for part in parts[1:])


def check_codes(section, entries, key="code"):
    codes = [entry[key] for entry in entries]
    if len(codes) != len(set(codes)):
        sys.exit(f"registry: duplicate {key} in {section}")
    if codes != sorted(codes):
        sys.exit(f"registry: {section} must be in ascending {key} order")


def enum(lines, name, comment, entries, base="std::uint8_t"):
    lines.append(f"// {comment}")
    lines.append(f"enum class {name} : {base} {{")
    for entry in entries:
        lines.append(f"    {entry['id']} = {entry['code']}u,")
    lines.append("};")
    lines.append("")


def generate(registry):
    for section in ("quantities", "events", "output_kinds", "commands", "services", "parameters", "boards"):
        check_codes(section, registry[section])
    check_codes("products", registry["products"], "product_code")
    manufacturer = registry["manufacturer_code"]
    industry = registry["industry_code"]
    header = manufacturer | (3 << 11) | (industry << 13)
    if header != registry["proprietary_header"]:
        sys.exit("registry: proprietary_header does not match manufacturer and industry code")

    lines = [
        "#pragma once",
        "",
        "// GENERATED from src/lib/b1s/registry.yaml by src/lib/b1s/tools/generate_registry.py.",
        "// Do not edit: change the registry and regenerate (rules: src/lib/b1s/README.md,",
        '// "Adding to B1S").',
        "",
        "#include <cstdint>",
        "",
        "namespace b1s {",
        "",
        f"inline constexpr std::uint16_t manufacturerCode = {manufacturer}u;",
        f"inline constexpr std::uint8_t industryCode = {industry}u;",
        f"inline constexpr std::uint16_t proprietaryHeader = 0x{header:04X}u; // sent as {header & 0xFF:02X} {header >> 8:02X}",
        "",
        "// B1S proprietary PGNs.",
        "namespace pgn {",
    ]
    for entry in registry["pgns"]:
        kind = "single frame" if entry["kind"] == "single" else "fast packet"
        lines.append(f"inline constexpr std::uint32_t {entry['id']} = {entry['pgn']}u; // {kind}, {entry['addressing']}")
    lines += ["} // namespace pgn", ""]

    enum(lines, "Quantity", "Quantities of the measurement messages; units and scales below.",
         registry["quantities"])
    lines.append("// Physical value = raw x scale, in the quantity's SI unit.")
    lines.append("constexpr double scale(Quantity quantity) {")
    lines.append("    switch (quantity) {")
    for entry in registry["quantities"]:
        lines.append(f"    case Quantity::{entry['id']}: return {float(entry['scale'])!r}; // {entry['unit']}")
    lines += ["    }", "    return 0.0;", "}", ""]
    lines.append("// True for quantities encoded as float32 instead of scaled int32.")
    lines.append("constexpr bool isFloat(Quantity quantity) {")
    floats = [entry["id"] for entry in registry["quantities"] if entry["encoding"] == "float32"]
    if floats:
        lines.append("    switch (quantity) {")
        for name in floats:
            lines.append(f"    case Quantity::{name}:")
        lines += ["        return true;", "    default:", "        return false;", "    }"]
    else:
        lines += ["    static_cast<void>(quantity);", "    return false;"]
    lines += ["}", ""]

    enum(lines, "EventType", "Event types of the Event message.", registry["events"])
    enum(lines, "OutputKind", "Output kinds of Output Status and Channel Description.", registry["output_kinds"])
    enum(lines, "Command", "Commands of the Command message; bit 7 marks a reply.", registry["commands"])
    enum(lines, "Service", "Service codes of the Service message.", registry["services"])
    enum(lines, "Board", "Boards of firmware images (FIRMWARE.md).", registry["boards"], "std::uint16_t")
    enum(lines, "Parameter", "Parameters for configuration over CAN (scope, type, unit: registry).",
         registry["parameters"], "std::uint16_t")
    sizes = {"uint8": 1, "enum": 1, "flags": 1, "uint16": 2, "int32": 4}
    records = registry.get("records", {})
    for entry in registry["parameters"]:
        if entry["type"] == "record" and entry.get("record") not in records:
            sys.exit(f"registry: parameter {entry['id']} names no record layout")
    lines.append("// Sizes of record parameters (registry: records).")
    lines.append("namespace record {")
    for name, fields in records.items():
        size = sum(sizes[field["type"]] for field in fields)
        if size > 32:
            sys.exit(f"registry: record {name} has more than 32 bytes")
        lines.append(f"inline constexpr std::uint8_t {name}Size = {size}u;")
    lines += ["} // namespace record", ""]
    lines.append("// True for parameters of a channel (addressed with a channel number).")
    lines.append("constexpr bool isChannelParameter(Parameter parameter) {")
    lines.append("    switch (parameter) {")
    for entry in registry["parameters"]:
        if entry["scope"] == "channel":
            lines.append(f"    case Parameter::{entry['id']}:")
    lines += ["        return true;", "    default:", "        return false;", "    }", "}", ""]

    lines += [
        "// Category of a standard NMEA 2000 PGN (registry: standard_pgns).",
        "enum class StandardCategory : std::uint8_t { unlisted, display, navigation };",
        "",
        "constexpr StandardCategory standardCategory(std::uint32_t pgn) {",
        "    switch (pgn) {",
    ]
    for category in ("display", "navigation"):
        for entry in registry["standard_pgns"]:
            if entry["category"] == category:
                lines.append(f"    case {entry['pgn']}u: // {entry['name']}")
        lines.append(f"        return StandardCategory::{category};")
    lines += ["    default:", "        return StandardCategory::unlisted;", "    }", "}", ""]

    lines += [
        "// Device types: product code (PGN 126996), preferred address, NMEA 2000",
        "// device class and function.",
        "struct Product {",
        "    std::uint16_t code;",
        "    std::uint8_t preferredAddress;",
        "    std::uint8_t deviceClass;",
        "    std::uint8_t deviceFunction;",
        "};",
        "",
        "namespace product {",
    ]
    for entry in registry["products"]:
        lines.append(f"inline constexpr Product {identifier(entry['id'])}{{{entry['product_code']}u, "
                     f"{entry['preferred_address']}u, {entry['device_class']}u, {entry['device_function']}u}};")
    lines += ["} // namespace product", "", "} // namespace b1s", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fail if the header is out of date")
    args = parser.parse_args()
    with REGISTRY.open() as file:
        text = generate(yaml.safe_load(file))
    if args.check:
        if not HEADER.exists() or HEADER.read_text() != text:
            sys.exit(f"{HEADER} is out of date; run {pathlib.Path(__file__).name}")
        return
    HEADER.parent.mkdir(parents=True, exist_ok=True)
    HEADER.write_text(text)
    print(f"wrote {HEADER}")


if __name__ == "__main__":
    main()
