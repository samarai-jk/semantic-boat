#include "slstm32/sensors/window.hpp"

namespace slstm32::sensors {

namespace {

constexpr std::uint16_t maxCount = 0xFFFFu;

std::int32_t meanOf(std::int64_t sum, std::uint32_t count) {
    const auto half = static_cast<std::int64_t>(count / 2u);
    return static_cast<std::int32_t>((sum >= 0 ? sum + half : sum - half) / static_cast<std::int64_t>(count));
}

} // namespace

const char* functionName(ValueFunction function) {
    switch (function) {
    case ValueFunction::none: return "none";
    case ValueFunction::last: return "last";
    case ValueFunction::average: return "average";
    case ValueFunction::minimum: return "minimum";
    case ValueFunction::maximum: return "maximum";
    case ValueFunction::change: return "change";
    case ValueFunction::rate: return "rate";
    }
    return "?";
}

bool Window::start(ValueFunction function, WindowLength length, std::uint32_t now) {
    if (length.length == 0u || capacity_ < 2u) return false;
    function_ = function;
    samples_ = length.samples;
    if (samples_) {
        // ceil(N / B) samples per bucket, as many buckets as N needs.
        span_ = (length.length + capacity_ - 1u) / capacity_;
        used_ = static_cast<std::uint16_t>((length.length + span_ - 1u) / span_);
    } else {
        span_ = static_cast<std::uint32_t>(length.length) * 1000u / capacity_;
        if (span_ == 0u) span_ = 1u;
        used_ = capacity_;
        openStart_ = now - now % span_;
    }
    head_ = filled_ = 0u;
    openCount_ = 0u;
    openSum_ = 0;
    openExtreme_ = 0;
    return true;
}

void Window::close() {
    std::int32_t value = 0;
    if (openCount_ != 0u) {
        value = function_ == ValueFunction::minimum || function_ == ValueFunction::maximum ? openExtreme_
                                                                                           : meanOf(openSum_, openCount_);
    }
    values_[head_] = value;
    counts_[head_] = static_cast<std::uint16_t>(openCount_ < maxCount ? openCount_ : maxCount);
    head_ = static_cast<std::uint16_t>((head_ + 1u) % used_);
    if (filled_ < used_) ++filled_;
    openCount_ = 0u;
    openSum_ = 0;
}

void Window::advance(std::uint32_t now) {
    if (samples_ || used_ == 0u) return;
    const std::uint32_t steps = (now - openStart_) / span_;
    if (steps == 0u) return;
    // After a long gap every bucket is empty; no need to close more than all.
    const std::uint32_t closes = steps < used_ ? steps : used_;
    for (std::uint32_t step = 0u; step < closes; ++step) close();
    openStart_ += steps * span_;
}

void Window::add(std::int32_t value, std::uint32_t now) {
    if (used_ == 0u) return;
    advance(now);
    if (openCount_ == 0u) {
        openExtreme_ = value;
    } else if (function_ == ValueFunction::minimum ? value < openExtreme_ : value > openExtreme_) {
        openExtreme_ = value;
    }
    openSum_ += value;
    ++openCount_;
    if (samples_ && openCount_ >= span_) close();
}

bool Window::result(std::int32_t& value) const {
    std::int64_t sum = openSum_;
    std::uint64_t count = openCount_;
    bool any = openCount_ != 0u;
    std::int32_t extreme = openExtreme_;
    bool oldestFound = false;
    std::int32_t oldest = 0;
    std::int32_t newest = any ? meanOf(openSum_, openCount_) : 0;
    bool newestFound = any;
    unsigned buckets = any ? 1u : 0u;
    // From the newest completed bucket back to the oldest.
    for (std::uint16_t step = 0u; step < filled_; ++step) {
        const auto index = static_cast<std::uint16_t>((head_ + used_ - 1u - step) % used_);
        if (counts_[index] == 0u) continue;
        const auto bucket = values_[index];
        sum += static_cast<std::int64_t>(bucket) * counts_[index];
        count += counts_[index];
        if (!any || (function_ == ValueFunction::minimum ? bucket < extreme : bucket > extreme)) extreme = bucket;
        any = true;
        ++buckets;
        if (!newestFound) {
            newest = bucket;
            newestFound = true;
        }
        oldest = bucket;
        oldestFound = true;
    }
    if (!any) return false;
    switch (function_) {
    case ValueFunction::minimum:
    case ValueFunction::maximum:
        value = extreme;
        return true;
    case ValueFunction::change:
        if (buckets < 2u || !oldestFound) return false;
        value = newest - oldest;
        return true;
    default:
        value = static_cast<std::int32_t>((sum >= 0 ? sum + static_cast<std::int64_t>(count / 2u)
                                                    : sum - static_cast<std::int64_t>(count / 2u)) /
                                          static_cast<std::int64_t>(count));
        return true;
    }
}

Window* WindowPool::acquire() {
    for (std::size_t index = 0u; index < count_; ++index) {
        auto& window = windows_[index];
        if (window.inUse_) continue;
        window = Window{};
        window.values_ = values_ + index * buckets_;
        window.counts_ = counts_ + index * buckets_;
        window.capacity_ = static_cast<std::uint16_t>(buckets_);
        window.inUse_ = true;
        return &window;
    }
    return nullptr;
}

void WindowPool::release(Window* window) {
    if (window) window->inUse_ = false;
}

std::size_t WindowPool::available() const {
    std::size_t count = 0u;
    for (std::size_t index = 0u; index < count_; ++index) count += windows_[index].inUse_ ? 0u : 1u;
    return count;
}

} // namespace slstm32::sensors
