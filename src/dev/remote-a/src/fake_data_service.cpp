#include "fake_data_service.hpp"

namespace remote_a {

bool FakeDataService::init() {
    if (!runtime_.millis || !package_.valid()) return false;
    sog_ = data_.findSource(package_, "signalk", "navigation.speedOverGround");
    cog_ = data_.findSource(package_, "signalk", "navigation.courseOverGroundTrue");
    depth_ = data_.findSource(package_, "signalk", "environment.depth.belowTransducer");
    temperature_ = data_.findSource(package_, "signalk", "environment.outside.temperature");
    voltage_ = data_.findSource(package_, "signalk", "electrical.batteries.house.voltage");
    updateAt_ = runtime_.millis();
    return true;
}

void FakeDataService::publish(std::uint16_t source, float value, std::uint32_t now) {
    if (source == semantic_display::noIndex ||
        !semantic_display::sourceIsSubscribed(package_, source, display_.activeSection()) ||
        !data_.setNumber(source, value, now)) return;
    display_.sourceUpdated(source);
}

void FakeDataService::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - updateAt_) < 0) return;
    updateAt_ = now + 500u;
    ++step_;
    publish(sog_, 3.0f + static_cast<float>(step_ % 20u) * 0.025f, now);
    publish(cog_, 2.0f + static_cast<float>(step_ % 30u) * 0.001f, now);
    publish(depth_, 8.0f + static_cast<float>(step_ % 10u) * 0.1f, now);
    publish(temperature_, 288.15f + static_cast<float>(step_ % 8u) * 0.1f, now);
    publish(voltage_, 12.4f + static_cast<float>(step_ % 5u) * 0.02f, now);
}

} // namespace remote_a
