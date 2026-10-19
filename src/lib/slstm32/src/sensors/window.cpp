#include "slstm32/sensors/window.hpp"

namespace slstm32::sensors {

namespace {

constexpr std::uint16_t maxCount = 0xFFFFu;
constexpr std::int32_t fullCircle = 360000; // thousandths of a degree

std::int32_t meanOf(std::int64_t sum, std::uint64_t count) {
    const auto half = static_cast<std::int64_t>(count / 2u);
    return static_cast<std::int32_t>((sum >= 0 ? sum + half : sum - half) / static_cast<std::int64_t>(count));
}

// The angle equal to value (mod 360) closest to reference.
std::int32_t near(std::int32_t value, std::int32_t reference) {
    auto difference = (value - reference) % fullCircle;
    if (difference > fullCircle / 2) difference -= fullCircle;
    if (difference < -fullCircle / 2) difference += fullCircle;
    return reference + difference;
}

std::int32_t normalized(std::int32_t angle) {
    angle %= fullCircle;
    return angle < 0 ? angle + fullCircle : angle;
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
    case ValueFunction::range: return "range";
    case ValueFunction::median: return "median";
    }
    return "?";
}

bool Window::start(ValueFunction function, WindowLength length, std::uint32_t now, bool circular) {
    // Range keeps minimum and maximum: two values per bucket.
    const std::uint16_t buckets = function == ValueFunction::range ? capacity_ / 2u : capacity_;
    if (length.length == 0u || buckets < 2u) return false;
    if (function == ValueFunction::median &&
        (!length.samples || length.length > buckets || length.length > maxMedianSamples)) {
        return false;
    }
    function_ = function;
    samples_ = length.samples;
    circular_ = circular;
    if (samples_) {
        // ceil(N / B) samples per bucket, as many buckets as N needs.
        span_ = (length.length + buckets - 1u) / buckets;
        used_ = static_cast<std::uint16_t>((length.length + span_ - 1u) / span_);
    } else {
        span_ = static_cast<std::uint32_t>(length.length) * 1000u / buckets;
        if (span_ == 0u) span_ = 1u;
        used_ = buckets;
        openStart_ = now - now % span_;
    }
    head_ = filled_ = 0u;
    openCount_ = 0u;
    openSum_ = 0;
    return true;
}

void Window::close() {
    std::int32_t value = 0;
    if (openCount_ != 0u) {
        switch (function_) {
        case ValueFunction::minimum: value = openMinimum_; break;
        case ValueFunction::maximum: value = openMaximum_; break;
        case ValueFunction::range:
            value = openMinimum_;
            values_[2u * head_ + 1u] = openMaximum_;
            break;
        default:
            value = meanOf(openSum_, openCount_);
            if (circular_) value = normalized(value);
            break;
        }
    }
    values_[function_ == ValueFunction::range ? 2u * head_ : head_] = value;
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
    if (circular_) {
        if (openCount_ == 0u) openFirst_ = normalized(value);
        value = near(value, openFirst_);
    }
    if (openCount_ == 0u || value < openMinimum_) openMinimum_ = value;
    if (openCount_ == 0u || value > openMaximum_) openMaximum_ = value;
    openSum_ += value;
    ++openCount_;
    if (samples_ && openCount_ >= span_) close();
}

bool Window::result(std::int32_t& value) const {
    const bool range = function_ == ValueFunction::range;
    // Buckets from the newest back to the oldest; the current one first.
    std::int32_t newest = 0;
    std::int32_t oldest = 0;
    std::int32_t minimum = openMinimum_;
    std::int32_t maximum = openMaximum_;
    std::int64_t sum = 0;
    std::uint64_t count = 0u;
    unsigned buckets = 0u;
    std::int32_t samples[maxMedianSamples];
    const auto take = [&](std::int32_t mean, std::int32_t low, std::int32_t high, std::uint32_t weight) {
        if (buckets == 0u) newest = mean;
        if (circular_) mean = near(mean, newest);
        oldest = mean;
        if (buckets == 0u || low < minimum) minimum = low;
        if (buckets == 0u || high > maximum) maximum = high;
        sum += static_cast<std::int64_t>(mean) * weight;
        count += weight;
        if (buckets < maxMedianSamples) samples[buckets] = mean;
        ++buckets;
    };
    if (openCount_ != 0u) {
        const auto mean = meanOf(openSum_, openCount_);
        take(circular_ ? normalized(mean) : mean, openMinimum_, openMaximum_, openCount_);
    }
    for (std::uint16_t step = 0u; step < filled_; ++step) {
        const auto index = static_cast<std::uint16_t>((head_ + used_ - 1u - step) % used_);
        if (counts_[index] == 0u) continue;
        const auto bucket = values_[range ? 2u * index : index];
        take(bucket, bucket, range ? values_[2u * index + 1u] : bucket, counts_[index]);
    }
    if (buckets == 0u) return false;
    switch (function_) {
    case ValueFunction::minimum: value = minimum; return true;
    case ValueFunction::maximum: value = maximum; return true;
    case ValueFunction::range: value = maximum - minimum; return true;
    case ValueFunction::change:
        if (buckets < 2u) return false;
        value = newest - oldest;
        return true;
    case ValueFunction::median: {
        // At most maxMedianSamples single-sample buckets (start() checks).
        const auto n = buckets < maxMedianSamples ? buckets : maxMedianSamples;
        for (unsigned i = 1u; i < n; ++i) {
            const auto key = samples[i];
            unsigned j = i;
            for (; j > 0u && samples[j - 1u] > key; --j) samples[j] = samples[j - 1u];
            samples[j] = key;
        }
        value = n % 2u ? samples[n / 2u] : meanOf(static_cast<std::int64_t>(samples[n / 2u - 1u]) + samples[n / 2u], 2u);
        return true;
    }
    default:
        value = meanOf(sum, count);
        if (circular_) value = normalized(value);
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
