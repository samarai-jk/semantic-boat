#pragma once

#include <cstdint>

// Standard NMEA 2000 PGNs used by the stack and by devices. Before a device
// sends a PGN that is not yet used, add it to standard_pgns in
// src/lib/b1s/registry.yaml with its category; B1S proprietary PGNs are
// allocated there too (rules: src/lib/b1s/README.md, "Adding to B1S").
namespace n2k::pgn {

// Network management
inline constexpr std::uint32_t isoAcknowledgement = 59392u;
inline constexpr std::uint32_t isoRequest = 59904u;
inline constexpr std::uint32_t isoAddressClaim = 60928u;
inline constexpr std::uint32_t pgnList = 126464u;
inline constexpr std::uint32_t heartbeat = 126993u;
inline constexpr std::uint32_t productInformation = 126996u;

// Navigation
inline constexpr std::uint32_t vesselHeading = 127250u;
inline constexpr std::uint32_t rateOfTurn = 127251u;
inline constexpr std::uint32_t attitude = 127257u;

// Propulsion
inline constexpr std::uint32_t engineRapidUpdate = 127488u;

// Electrical
inline constexpr std::uint32_t binarySwitchBankStatus = 127501u;
inline constexpr std::uint32_t batteryStatus = 127508u;

// Environment
inline constexpr std::uint32_t humidity = 130313u;
inline constexpr std::uint32_t actualPressure = 130314u;
inline constexpr std::uint32_t temperatureExtendedRange = 130316u;

} // namespace n2k::pgn
