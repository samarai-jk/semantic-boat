#pragma once

#include <cstddef>
#include <cstdint>

namespace slstm32::epd {

// Coalesces render requests by logical region. A bit requested while the same
// region is active remains pending, so completing the old update cannot erase
// newer work.
template <std::size_t RegionCount>
class RenderQueue {
public:
    static_assert(RegionCount > 0u && RegionCount <= 32u,
                  "RenderQueue supports between 1 and 32 regions");
    using Mask = std::uint32_t;

    bool request(std::uint8_t region) {
        if (region >= RegionCount) return false;
        pending_ |= static_cast<Mask>(1u) << region;
        return true;
    }

    bool pending() const { return pending_ != 0u; }
    bool active() const { return active_ != 0u; }
    Mask pendingMask() const { return pending_; }
    Mask activeMask() const { return active_; }

    Mask begin() {
        if (active_ != 0u) return 0u;
        active_ = pending_;
        pending_ = 0u;
        return active_;
    }

    void complete() { active_ = 0u; }

    void retryActive() {
        pending_ |= active_;
        active_ = 0u;
    }

private:
    Mask pending_{};
    Mask active_{};
};

} // namespace slstm32::epd
