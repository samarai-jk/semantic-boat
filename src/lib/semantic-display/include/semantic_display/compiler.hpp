#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include "semantic_display/package.hpp"

namespace semantic_display {

enum class CompileError : std::uint8_t {
    none,
    invalidJson,
    unsupportedSchema,
    missingProperty,
    invalidProperty,
    duplicateId,
    unresolvedSource,
    invalidLayout,
    outputTooLarge,
    historyBudgetExceeded,
    dataBudgetExceeded,
    sourceTooLarge,
};

struct CompileLimits {
    std::size_t maxSourceBytes{32u * 1024u};
    std::size_t maxPackageBytes{16192u};
    std::size_t maxHistoryBytes{8u * 1024u};
    std::uint8_t maxStringBytes{255u};
    std::uint8_t maxJsonDepth{16u};
    std::uint16_t maxSources{noIndex};
};

struct CompileResult {
    CompileError error{CompileError::none};
    std::size_t inputOffset{};
    std::size_t packageSize{};
    const char* message{"ok"};
    explicit operator bool() const { return error == CompileError::none; }
};

class ConfigCompiler {
public:
    explicit ConfigCompiler(CompileLimits limits = {}) : limits_(limits) {}
    CompileResult compile(std::string_view json, std::uint8_t* output,
                          std::size_t capacity) const;

private:
    CompileLimits limits_;
};

} // namespace semantic_display
