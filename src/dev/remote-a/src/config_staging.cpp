#include "config_staging.hpp"
#include "semantic_display/package.hpp"

namespace remote_a {
namespace {

constexpr std::uint32_t stagingMagic = 0x4a434253u; // "SBCJ"
constexpr std::uint32_t stagingVersion = 1u;

std::uint32_t read32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8u) |
           (static_cast<std::uint32_t>(data[2]) << 16u) | (static_cast<std::uint32_t>(data[3]) << 24u);
}

} // namespace

StagedConfiguration stagedConfiguration() {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(configStagingAddress);
    if (read32(bytes) != stagingMagic || read32(bytes + 4u) != stagingVersion) return {};
    const auto length = read32(bytes + 8u);
    const auto expectedCrc = read32(bytes + 12u);
    if (length == 0u || length > maxStagedConfigBytes) return {};
    const auto* json = bytes + configStagingHeaderBytes;
    if (semantic_display::crc32(json, length) != expectedCrc) return {};
    return {{reinterpret_cast<const char*>(json), length}, expectedCrc, true};
}

} // namespace remote_a
