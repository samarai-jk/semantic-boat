#pragma once

#include "semantic_display/data.hpp"
#include "semantic_display/display.hpp"
#include "semantic_display/package.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace remote_a {

class FakeDataService final : public slstm32::Service {
public:
    FakeDataService(slstm32::Runtime runtime, semantic_display::PackageView& package,
                    semantic_display::DataStore& data,
                    semantic_display::DisplayService& display)
        : runtime_(runtime), package_(package), data_(data), display_(display) {}
    bool init() override;
    void run() override;

private:
    void publish(std::uint16_t source, float value, std::uint32_t now);
    slstm32::Runtime runtime_;
    semantic_display::PackageView& package_;
    semantic_display::DataStore& data_;
    semantic_display::DisplayService& display_;
    std::uint16_t sog_{semantic_display::noIndex};
    std::uint16_t cog_{semantic_display::noIndex};
    std::uint16_t depth_{semantic_display::noIndex};
    std::uint16_t temperature_{semantic_display::noIndex};
    std::uint16_t voltage_{semantic_display::noIndex};
    std::uint32_t updateAt_{};
    std::uint32_t step_{};
};

} // namespace remote_a
