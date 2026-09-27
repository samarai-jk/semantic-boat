#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "display_link_service.hpp"
#include "feedback_service.hpp"
#include "semantic_display/alert.hpp"
#include "semantic_display/display.hpp"
#include "slstm32/service.hpp"

namespace remote_a {

class AlarmService final : public slstm32::Service {
public:
    static constexpr std::size_t capacity = 8u;

    AlarmService(semantic_display::DisplayService& display,
                 FeedbackService& feedback, DisplayLinkService& link)
        : display_(display), feedback_(feedback), link_(link) {}

    bool init() override;
    void run() override {}
    void setMinimumLevel(semantic_display::AlertLevel level);

private:
    static constexpr std::size_t idCapacity = 48u;
    static constexpr std::size_t titleCapacity = 48u;
    static constexpr std::size_t messageCapacity = 128u;

    struct Record {
        std::array<char, idCapacity> id{};
        std::array<char, titleCapacity> title{};
        std::array<char, messageCapacity> message{};
        std::uint32_t occurrence{};
        std::uint32_t order{};
        semantic_display::AlertLevel level{semantic_display::AlertLevel::info};
        bool active{};
        bool hidden{};
        bool locallyHidden{};
        bool globalActionPending{};
        bool silenced{};
    };

    static void updateThunk(void*, std::uint8_t level, std::uint32_t occurrence,
                            std::string_view id, std::string_view title,
                            std::string_view message);
    static void removeThunk(void*, std::string_view id);
    static void silenceThunk(void*, std::string_view id);
    static void actionThunk(void*, semantic_display::AlertAction action);
    static void presentedThunk(void*);

    void update(std::uint8_t level, std::uint32_t occurrence,
                std::string_view id, std::string_view title,
                std::string_view message);
    void remove(std::string_view id);
    void silence(std::string_view id);
    void action(semantic_display::AlertAction action);
    void presented();
    void showCurrent(bool resetFeedback = true);
    std::size_t selectCurrent() const;
    std::size_t find(std::string_view id) const;
    std::size_t allocate(semantic_display::AlertLevel incoming);
    std::uint8_t visibleCount() const;
    static void copy(std::string_view source, char* target, std::size_t capacity);
    static bool equals(const char* stored, std::string_view value);

    semantic_display::DisplayService& display_;
    FeedbackService& feedback_;
    DisplayLinkService& link_;
    std::array<Record, capacity> records_{};
    semantic_display::AlertLevel minimumLevel_{semantic_display::AlertLevel::info};
    std::uint32_t nextOrder_{1u};
    std::uint16_t overflowCount_{};
    std::size_t current_{capacity};
};

} // namespace remote_a
