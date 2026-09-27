#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "display_link_service.hpp"
#include "semantic_display/compiler.hpp"
#include "semantic_display/display.hpp"
#include "semantic_display/package.hpp"
#include "semantic_display/storage.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace remote_a {

class ConfigurationService final : public slstm32::Service {
public:
    static constexpr std::size_t maxConfigurations = 16u;
    static constexpr std::size_t maxNameBytes = 47u;

    using FallbackConfiguration = std::string_view (*)();

    ConfigurationService(slstm32::Runtime runtime, DisplayLinkService& link,
                         semantic_display::DisplayService& display,
                         semantic_display::ConfigCompiler& compiler,
                         semantic_display::ConfigStore& store,
                         semantic_display::PackageView& package,
                         std::uint8_t* packageBuffer, std::size_t packageCapacity,
                         char* sourceBuffer, std::size_t sourceCapacity,
                         FallbackConfiguration fallback)
        : runtime_(runtime), link_(link), display_(display), compiler_(compiler), store_(store),
          package_(package), packageBuffer_(packageBuffer),
          packageCapacity_(packageCapacity), sourceBuffer_(sourceBuffer),
          sourceCapacity_(sourceCapacity), fallback_(fallback) {}

    bool init() override;
    void run() override;
    void requestList();

private:
    struct Name {
        char bytes[maxNameBytes + 1u]{};
    };

    static void receiveListBegin(void* context, std::uint16_t listId,
                                 std::uint16_t count);
    static void receiveListItem(void* context, std::uint16_t listId,
                                std::uint16_t index, std::string_view name);
    static void receiveListEnd(void* context, std::uint16_t listId);
    static void receiveConfigBegin(void* context, std::uint16_t transferId,
                                   std::uint32_t size, std::uint32_t crc,
                                   std::string_view name);
    static void receiveConfigChunk(void* context, std::uint16_t transferId,
                                   std::uint32_t offset, const std::uint8_t* data,
                                   std::size_t size);
    static void receiveConfigEnd(void* context, std::uint16_t transferId);
    static std::string_view selectionItem(void* context, std::size_t index);
    static void selectionCompleted(void* context,
                                   semantic_display::SelectionResult result,
                                   std::size_t index);

    void listBegin(std::uint16_t listId, std::uint16_t count);
    void listItem(std::uint16_t listId, std::uint16_t index,
                  std::string_view name);
    void listEnd(std::uint16_t listId);
    void configBegin(std::uint16_t transferId, std::uint32_t size,
                     std::uint32_t crc, std::string_view name);
    void configChunk(std::uint16_t transferId, std::uint32_t offset,
                     const std::uint8_t* data, std::size_t size);
    void configEnd(std::uint16_t transferId);
    void loadDefault();
    bool activate(std::size_t packageSize);
    void restoreFallback();
    void transferError(const char* message);

    slstm32::Runtime runtime_;
    DisplayLinkService& link_;
    semantic_display::DisplayService& display_;
    semantic_display::ConfigCompiler& compiler_;
    semantic_display::ConfigStore& store_;
    semantic_display::PackageView& package_;
    std::uint8_t* packageBuffer_{};
    std::size_t packageCapacity_{};
    char* sourceBuffer_{};
    std::size_t sourceCapacity_{};
    FallbackConfiguration fallback_{};
    std::array<Name, maxConfigurations> names_{};
    Name transferName_{};
    std::size_t nameCount_{};
    std::size_t receivedBytes_{};
    std::uint32_t expectedBytes_{};
    std::uint32_t expectedCrc_{};
    std::uint32_t lastTransferAt_{};
    std::uint16_t listId_{};
    std::uint16_t transferId_{};
    bool listActive_{};
    bool transferActive_{};
    bool transferFailed_{};
};

} // namespace remote_a
