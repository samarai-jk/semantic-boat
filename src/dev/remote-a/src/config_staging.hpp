#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace remote_a {

inline constexpr std::uintptr_t configStagingAddress = 0x0803c000u;
inline constexpr std::size_t configStagingBytes = 16u * 1024u;
inline constexpr std::size_t configStagingHeaderBytes = 16u;
inline constexpr std::size_t maxStagedConfigBytes = configStagingBytes - configStagingHeaderBytes;

struct StagedConfiguration {
    std::string_view json{};
    std::uint32_t sourceCrc32{};
    bool valid{};
};

StagedConfiguration stagedConfiguration();

} // namespace remote_a
