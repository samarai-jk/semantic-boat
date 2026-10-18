#pragma once

#include <cstdint>

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
