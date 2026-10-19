// Host tests of measurement windows, alarm rules, and their persistence.
// Build and run (no firmware toolchain needed), from the repository root:
//   g++ -std=c++17 -Wall -Wextra -Isrc/lib/slstm32/include src/lib/slstm32/tests/sensors_tests.cpp
//       src/lib/slstm32/src/sensors/{window,measurement,registry}.cpp src/lib/slstm32/src/{event_bus,log}.cpp
//       src/lib/slstm32/src/storage/{keyed_record_store,redundant_blob_store}.cpp -o build/sensors_tests
//       && build/sensors_tests
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "slstm32/sensors/registry.hpp"
#include "slstm32/sensors/window.hpp"
#include "slstm32/storage/byte_storage.hpp"

using namespace slstm32::sensors;

namespace {

std::uint32_t clock = 0u;
std::uint32_t millis() { return clock; }

class MemoryStorage final : public slstm32::storage::ByteStorage {
public:
    explicit MemoryStorage(std::size_t size) : bytes_(size, 0xFFu) {}
    std::size_t capacity() const override { return bytes_.size(); }
    bool read(std::uint32_t address, std::uint8_t* data, std::size_t size) override {
        std::memcpy(data, bytes_.data() + address, size);
        return true;
    }
    bool write(std::uint32_t address, const std::uint8_t* data, std::size_t size) override {
        std::memcpy(bytes_.data() + address, data, size);
        return true;
    }

private:
    std::vector<std::uint8_t> bytes_;
};

struct Transitions {
    int activated{};
    int cleared{};
};

void count(void* context, Measurement&, std::size_t, AlarmTransition transition) {
    auto& transitions = *static_cast<Transitions*>(context);
    if (transition == AlarmTransition::activated) ++transitions.activated;
    if (transition == AlarmTransition::cleared) ++transitions.cleared;
}

void windowTests() {
    StaticWindowPool<3, 4> pool;
    assert(pool.available() == 3u && pool.buckets() == 4u);
    // Sample windows of at most B samples are exact.
    auto* last3 = pool.acquire();
    assert(last3->start(ValueFunction::average, {3u, true}, 0u));
    std::int32_t value = 0;
    assert(!last3->result(value));
    for (int sample : {10, 20, 30, 40}) last3->add(sample, 0u);
    assert(last3->result(value) && value == 30); // 20, 30, 40
    // 10 samples in 4 buckets of 3: covers 9..12 samples, here the 12 newest of 1..12 minus nothing.
    auto* max10 = pool.acquire();
    assert(max10->start(ValueFunction::maximum, {10u, true}, 0u));
    for (int sample = 1; sample <= 12; ++sample) max10->add(sample == 3 ? 100 : sample, 0u);
    assert(max10->result(value) && value == 100);
    for (int sample = 13; sample <= 24; ++sample) max10->add(sample, 0u);
    assert(max10->result(value) && value == 24);
    // Time window of 8 s in 4 buckets of 2 s.
    auto* minimum = pool.acquire();
    assert(pool.available() == 0u && pool.acquire() == nullptr);
    assert(minimum->start(ValueFunction::minimum, {8u, false}, 1000u));
    minimum->add(5, 1000u);   // bucket 0..2 s
    minimum->add(50, 3000u);  // bucket 2..4 s
    assert(minimum->result(value) && value == 5);
    minimum->advance(11000u); // 5 (bucket 0..2 s) has dropped out; 50 (2..4 s) is still in
    minimum->add(60, 11000u);
    assert(minimum->result(value) && value == 50);
    minimum->advance(13000u); // now 50 is out too
    assert(minimum->result(value) && value == 60);
    minimum->advance(100000u); // a long gap empties it
    assert(!minimum->result(value));
    WindowPool::release(last3);
    assert(pool.available() == 1u);
    // Change: newest minus oldest bucket mean.
    auto* change = pool.acquire();
    assert(change->start(ValueFunction::change, {4u, true}, 0u));
    change->add(1000, 0u);
    assert(!change->result(value));
    change->add(1500, 0u);
    change->add(2000, 0u);
    assert(change->result(value) && value == 1000);
}

void statisticsTests() {
    StaticWindowPool<4, 8> pool;
    std::int32_t value = 0;
    // Range over 6 samples: range uses two values per bucket (4 buckets of 2 samples).
    auto* range = pool.acquire();
    assert(range->start(ValueFunction::range, {6u, true}, 0u));
    for (int sample : {10, 40, 20, 25}) range->add(sample, 0u);
    assert(range->result(value) && value == 30);
    for (int sample : {21, 22, 23, 24, 25, 26}) range->add(sample, 0u);
    assert(range->result(value) && value == 5);
    // Median of 5: a single spike is dropped.
    auto* median = pool.acquire();
    assert(!median->start(ValueFunction::median, {5u, false}, 0u)); // samples only
    assert(!median->start(ValueFunction::median, {9u, true}, 0u));  // at most the bucket count
    assert(median->start(ValueFunction::median, {5u, true}, 0u));
    for (int sample : {100, 101, 900, 102, 99}) median->add(sample, 0u);
    assert(median->result(value) && value == 101);
    median->add(103, 0u); // 101, 900, 102, 99, 103
    assert(median->result(value) && value == 102);
    // Directions average across 0/360: 359 and 1 give 0, not 180.
    auto* heading = pool.acquire();
    assert(heading->start(ValueFunction::average, {4u, true}, 0u, true));
    heading->add(359000, 0u);
    heading->add(1000, 0u);
    assert(heading->result(value) && value == 0);
    heading->add(3000, 0u);
    heading->add(5000, 0u); // 359, 1, 3, 5 -> 2
    assert(heading->result(value) && value == 2000);
    auto* time = pool.acquire();
    assert(time->start(ValueFunction::average, {8u, false}, 0u, true));
    for (std::uint32_t at = 0u; at < 8000u; at += 500u) time->add(at < 4000u ? 350000 : 10000, at); // 350 and 10
    assert(time->result(value) && value == 0);
    // Channel rules: a circular channel allows last and average only; the output is a reading.
    MeasurementConfig config{"heading", Quantity::angle};
    config.circular = true;
    Measurement channel{config};
    StaticWindowPool<2, 8> windows;
    AlarmRule rule{ValueFunction::maximum, Comparison::above, Severity::warning, false, {10u, false}, 0u, 0, 0, 0};
    assert(channel.setAlarmRule(0u, rule, &windows, 0u) == Measurement::RuleResult::invalid);
    rule.function = ValueFunction::average;
    assert(channel.setAlarmRule(0u, rule, &windows, 0u) == Measurement::RuleResult::applied);
    assert(channel.setOutputRule({ValueFunction::range, {10u, false}}, &windows, 0u) == Measurement::RuleResult::invalid);
    assert(channel.setOutputRule({ValueFunction::median, {16u, true}}, &windows, 0u) == Measurement::RuleResult::invalid);
    assert(windows.available() == 1u);
}

void ruleTests() {
    StaticWindowPool<4, 8> pool;
    const MeasurementConfig config{"t", Quantity::voltage};
    Measurement channel{config};
    Transitions transitions;
    channel.setAlarmListener(&count, &transitions);
    // Warning when the average of the last 2 samples is below 11.8 V for 2 s.
    AlarmRule low{ValueFunction::average, Comparison::below, Severity::warning, false, {2u, true}, 2u, 11800, 0, 200};
    assert(channel.setAlarmRule(0u, low, &pool, 0u) == Measurement::RuleResult::applied);
    assert(pool.available() == 3u);
    channel.update(12000, 0u);
    channel.update(11000, 1000u); // average 11.5: pending
    assert(!channel.alarmActive(0u));
    channel.update(11000, 3000u);
    assert(channel.alarmActive(0u) && transitions.activated == 1);
    channel.update(11900, 4000u); // average 11.45 .. hysteresis: still below 12.0
    assert(channel.alarmActive(0u));
    channel.update(12100, 5000u); // average 12.0: not below 11.8 + 0.2
    assert(!channel.alarmActive(0u) && transitions.cleared == 1);
    // Band: alarm inside 1800..2100 at once.
    AlarmRule band{ValueFunction::last, Comparison::inside, Severity::alarm, false, {}, 0u, 1800, 2100, 0};
    assert(channel.setAlarmRule(1u, band, &pool, 5000u) == Measurement::RuleResult::applied);
    assert(pool.available() == 3u); // last needs no window
    channel.update(1900, 6000u);
    assert(channel.alarmActive(1u) && channel.alarmSeverity(1u) == Severity::alarm);
    channel.update(2200, 7000u);
    assert(!channel.alarmActive(1u));
    // Outside a band; an invalid band is refused.
    band.comparison = Comparison::outside;
    band.upper = 1000;
    assert(channel.setAlarmRule(1u, band, &pool, 7000u) == Measurement::RuleResult::invalid);
    band.upper = 2100;
    assert(channel.setAlarmRule(1u, band, &pool, 7000u) == Measurement::RuleResult::applied);
    channel.update(2200, 8000u);
    assert(channel.alarmActive(1u));
    // A changed rule clears its active alarm.
    const auto cleared = transitions.cleared;
    band.severity = Severity::emergency;
    assert(channel.setAlarmRule(1u, band, &pool, 8000u) == Measurement::RuleResult::applied);
    assert(!channel.alarmActive(1u) && transitions.cleared == cleared + 1);
    // Removing a windowed rule returns its window.
    assert(channel.setAlarmRule(0u, AlarmRule{}, &pool, 8000u) == Measurement::RuleResult::applied);
    assert(pool.available() == 4u && !channel.alarmUsed(0u));
    // Output: maximum of the last 3 samples.
    assert(channel.setOutputRule({ValueFunction::maximum, {3u, true}}, &pool, 8000u) == Measurement::RuleResult::applied);
    channel.update(10, 9000u);
    channel.update(30, 9100u);
    channel.update(20, 9200u);
    std::int32_t value = 0;
    assert(channel.output(value) && value == 30 && channel.rawValue() == 20);
    // The pool runs out: nothing changes.
    for (std::size_t slot = 0u; slot < 3u; ++slot) {
        AlarmRule windowedRule{ValueFunction::average, Comparison::above, Severity::warning, false, {10u, false}, 0u, 0, 0, 0};
        assert(channel.setAlarmRule(slot == 1u ? 3u : slot, windowedRule, &pool, 9200u) == Measurement::RuleResult::applied);
    }
    AlarmRule more{ValueFunction::minimum, Comparison::above, Severity::warning, false, {10u, false}, 0u, 0, 0, 0};
    assert(channel.setAlarmRule(1u, more, &pool, 9200u) == Measurement::RuleResult::noWindow);
    assert(channel.alarmRule(1u).severity == Severity::emergency);
    // Fault alarm: emergency after 2 s without a value.
    assert(channel.setFaultRule({true, Severity::emergency, 2u, false}) == Measurement::RuleResult::applied);
    channel.fail(Fault::noResponse, 10000u);
    channel.tick(11000u);
    assert(!channel.alarmActive(Measurement::faultSlot));
    channel.tick(12000u);
    assert(channel.alarmActive(Measurement::faultSlot) && channel.alarmSeverity(Measurement::faultSlot) == Severity::emergency);
    assert(!channel.output(value));
}

void persistenceTests() {
    MemoryStorage storage{8192u};
    StaticWindowPool<4, 8> pool;
    constexpr AlarmRule defaults[]{{ValueFunction::last, Comparison::above, Severity::warning, false, {}, 0u, 40000, 0, 1000}};
    const MeasurementConfig config{"board", Quantity::temperature, 0u, 10000u, 0u, defaults, 1u,
                                   {true, Severity::alarm, 5u, false}};
    {
        Measurement channel{config};
        Measurement* channels[]{&channel};
        MeasurementRegistry registry{{&millis}, {}, channels, 1u, nullptr, nullptr, 0u, &pool, &storage, 0u};
        assert(registry.init());
        assert(channel.alarmRule(0u).threshold == 40000 && channel.faultRule().severity == Severity::alarm);
        AlarmRule rule{ValueFunction::average, Comparison::atLeast, Severity::emergency, true, {60u, false}, 30u, 50000, 0, 500};
        assert(registry.setAlarmRule(0u, 2u, rule) == MeasurementRegistry::RuleResult::stored);
        assert(registry.setAlarmRule(0u, 0u, AlarmRule{}) == MeasurementRegistry::RuleResult::stored);
        assert(registry.setOutputRule(0u, {ValueFunction::average, {10u, false}}) == MeasurementRegistry::RuleResult::stored);
        assert(registry.setFaultRule(0u, {false, Severity::warning, 0u, false}) == MeasurementRegistry::RuleResult::stored);
        assert(pool.available() == 2u);
    }
    StaticWindowPool<4, 8> pool2;
    Measurement channel{config};
    Measurement* channels[]{&channel};
    MeasurementRegistry registry{{&millis}, {}, channels, 1u, nullptr, nullptr, 0u, &pool2, &storage, 0u};
    assert(registry.init());
    assert(!channel.alarmUsed(0u) && channel.alarmUsed(2u));
    const auto& rule = channel.alarmRule(2u);
    assert(rule.function == ValueFunction::average && rule.comparison == Comparison::atLeast && rule.latching &&
           rule.window.length == 60u && rule.delaySeconds == 30u && rule.threshold == 50000 && rule.hysteresis == 500);
    assert(channel.outputRule().function == ValueFunction::average && !channel.faultRule().enabled);
    assert(pool2.available() == 2u);
    // Another channel name at the same place ignores the records.
    const MeasurementConfig other{"mcu", Quantity::temperature, 1u, 10000u, 0u, defaults, 1u};
    Measurement otherChannel{other};
    Measurement* otherChannels[]{&otherChannel};
    StaticWindowPool<4, 8> pool3;
    MeasurementRegistry otherRegistry{{&millis}, {}, otherChannels, 1u, nullptr, nullptr, 0u, &pool3, &storage, 0u};
    assert(otherRegistry.init());
    assert(otherChannel.alarmRule(0u).threshold == 40000 && !otherChannel.alarmUsed(2u));
    // Reset: the defaults again, windows returned.
    assert(registry.resetProcessing(0u));
    assert(channel.alarmRule(0u).threshold == 40000 && !channel.alarmUsed(2u) && channel.faultRule().enabled);
    assert(pool2.available() == 4u);
}

} // namespace

int main() {
    windowTests();
    statisticsTests();
    ruleTests();
    persistenceTests();
    std::puts("sensors tests passed");
    return 0;
}
