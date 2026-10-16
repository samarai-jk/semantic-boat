#include "activity_service.hpp"
#include "n2k/electrical.hpp"
#include "n2k/pgn.hpp"

namespace lab_test_01 {
namespace {

constexpr std::uint32_t publishPeriodMs = 1000u;
constexpr std::uint8_t electricalPriority = 6u;
constexpr std::uint8_t switchBank = 0u;

} // namespace

bool ActivityService::init() {
    greenLed_.blink(2000u, 60u);
    nextPublishAt_ = runtime_.millis() + publishPeriodMs;
    return true;
}

void ActivityService::run() {
    if (!alarmShown_ && health_.issueSeen()) {
        alarmShown_ = true;
        redLed_.blink(1000u, 100u);
    }
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextPublishAt_) < 0) return;
    nextPublishAt_ += publishPeriodMs;
    if (node_.claimed()) publish();
}

void ActivityService::publish() {
    std::uint8_t data[8];
    for (std::size_t channel = 0u; channel < semantic_modules::AdInputIso12v::channels; ++channel) {
        if (!module_.analogValid(channel)) continue;
        n2k::encodeBatteryStatus(data, static_cast<std::uint8_t>(channel), module_.analogMillivolts(channel));
        (void)node_.sendSingle(electricalPriority, n2k::pgn::batteryStatus, n2k::broadcastAddress, data, sizeof data);
    }
    bool states[semantic_modules::AdInputIso12v::channels]{};
    for (std::size_t channel = 0u; channel < semantic_modules::AdInputIso12v::channels; ++channel) {
        states[channel] = module_.digitalActive(channel);
    }
    n2k::encodeBinarySwitchBankStatus(data, switchBank, states, semantic_modules::AdInputIso12v::channels);
    (void)node_.sendSingle(electricalPriority, n2k::pgn::binarySwitchBankStatus, n2k::broadcastAddress, data,
                           sizeof data);
}

} // namespace lab_test_01
