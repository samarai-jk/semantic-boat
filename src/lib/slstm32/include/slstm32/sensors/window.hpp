#pragma once

#include <cstddef>
#include <cstdint>

namespace slstm32::sensors {

// How a value is derived from the samples of a channel: for its output (what
// is published and shown) and for each alarm rule.
enum class ValueFunction : std::uint8_t {
    none,    // alarm slot unused
    last,    // latest calibrated sample
    average, // over the window
    minimum, // over the window
    maximum, // over the window
    change,  // over the window: newest minus oldest bucket mean (a trend)
    rate,    // change per minute over the last minute (smoothed value)
};

const char* functionName(ValueFunction function);
constexpr bool windowed(ValueFunction function) {
    return function == ValueFunction::average || function == ValueFunction::minimum ||
           function == ValueFunction::maximum || function == ValueFunction::change;
}

// Length of a window: seconds, or a number of samples.
struct WindowLength {
    std::uint16_t length{};
    bool samples{};
};

// A sliding window over a channel's samples with constant memory: B buckets
// (the pool's bucket count) of window / B each, plus the bucket being
// filled. A bucket keeps one value (its mean, minimum, or maximum, as the
// function needs) and its sample count, so 15 seconds and 6 hours cost the
// same. The result covers the last B completed buckets and the current one:
// the window plus at most one bucket (precision window / B). Sample windows
// of at most B samples are exact. Time buckets start at multiples of their
// length, so a value read at multiples of the window covers one window.
class Window {
public:
    // Starts empty; false if the length is 0.
    bool start(ValueFunction function, WindowLength length, std::uint32_t now);
    void add(std::int32_t value, std::uint32_t now);
    // Closes the buckets whose time is over (time windows); call regularly.
    void advance(std::uint32_t now);
    // False while the window holds no sample (or, for change, fewer than two buckets).
    bool result(std::int32_t& value) const;

private:
    friend class WindowPool;
    void close();

    std::int32_t* values_{};
    std::uint16_t* counts_{};
    std::uint16_t capacity_{};
    std::uint16_t used_{};
    std::uint16_t head_{};   // next bucket to write
    std::uint16_t filled_{}; // completed buckets
    ValueFunction function_{ValueFunction::none};
    bool samples_{};
    bool inUse_{};
    std::uint32_t span_{};      // bucket length: milliseconds or samples
    std::uint32_t openStart_{}; // time windows: start of the current bucket
    std::uint32_t openCount_{};
    std::int64_t openSum_{};
    std::int32_t openExtreme_{};
};

// The windows a device can give its channels: Count windows of Buckets
// buckets each, chosen per firmware (StaticWindowPool). A display without
// sensors has none; a monitoring device can spend its memory on many windows
// with fine buckets. RAM: about 48 + 6 x Buckets bytes per window.
class WindowPool {
public:
    // A free window, or nullptr.
    Window* acquire();
    static void release(Window* window);
    std::size_t size() const { return count_; }
    std::size_t available() const;
    std::size_t buckets() const { return buckets_; }

protected:
    // The arrays belong to the derived class and are not constructed yet:
    // only their addresses are kept here.
    WindowPool(Window* windows, std::size_t count, std::int32_t* values, std::uint16_t* counts, std::size_t buckets)
        : windows_(windows), count_(count), values_(values), counts_(counts), buckets_(buckets) {}

private:
    Window* windows_{};
    std::size_t count_{};
    std::int32_t* values_{};
    std::uint16_t* counts_{};
    std::size_t buckets_{};
};

template <std::size_t Count, std::size_t Buckets>
class StaticWindowPool final : public WindowPool {
    static_assert(Count >= 1u && Buckets >= 2u && Buckets <= 255u, "1+ windows of 2..255 buckets");

public:
    StaticWindowPool() : WindowPool(windows_, Count, values_, counts_, Buckets) {}

private:
    Window windows_[Count]{};
    std::int32_t values_[Count * Buckets]{};
    std::uint16_t counts_[Count * Buckets]{};
};

} // namespace slstm32::sensors
