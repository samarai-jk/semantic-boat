// Host tests of the B1S encoders and unit conversions. Build and run (no
// firmware toolchain needed), from the repository root:
//   g++ -std=c++17 -Wall -Wextra -Isrc/lib/b1s/include -Isrc/lib/n2k/include
//       -Isrc/lib/slstm32/include src/lib/b1s/tests/b1s_tests.cpp src/lib/b1s/src/quantity.cpp
//       src/lib/b1s/src/channel_settings.cpp src/lib/slstm32/src/storage/redundant_blob_store.cpp
//       -o build/b1s_tests && build/b1s_tests
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "b1s/channel_settings.hpp"
#include "b1s/messages.hpp"
#include "b1s/quantity.hpp"

namespace {

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
    std::vector<std::uint8_t> bytes_;
};

// Overrides and texts survive a reload in both layouts; the packed one keeps
// the neighbours of a channel in the same item.
void channelSettingsTests() {
    using b1s::ChannelSettings;
    assert(ChannelSettings::requiredBytes(24u) == 73u * 128u);
    assert(ChannelSettings::requiredBytes(29u, ChannelSettings::Layout::packed) == 65u * 128u);
    for (const auto layout : {ChannelSettings::Layout::perChannel, ChannelSettings::Layout::packed}) {
        MemoryStorage storage(ChannelSettings::requiredBytes(29u, layout));
        ChannelSettings::Override overrides[29u]{};
        ChannelSettings settings(storage, 0u, overrides, layout);
        settings.load();
        assert(overrides[7].onChange == ChannelSettings::notSet && overrides[7].period == ChannelSettings::periodNotSet);
        ChannelSettings::Override changed{};
        changed.onChange = 1u;
        changed.period = 1200u;
        assert(settings.writeOverride(7u, changed));
        changed.onChange = 0u;
        changed.rawPeriod = 0u;
        assert(settings.writeOverride(8u, changed));
        assert(settings.writeOverride(28u, changed));
        assert(!settings.writeOverride(29u, changed));
        assert(settings.writeText(ChannelSettings::Text::label, 8u, "Ignition"));
        ChannelSettings::Override reloaded[29u]{};
        ChannelSettings again(storage, 0u, reloaded, layout);
        again.load();
        assert(reloaded[7].onChange == 1u && reloaded[7].period == 1200u && reloaded[7].rawPeriod == 0xFFFFu);
        assert(reloaded[8].onChange == 0u && reloaded[8].rawPeriod == 0u && reloaded[28].period == 1200u);
        assert(reloaded[6].publish == ChannelSettings::notSet && reloaded[9].period == ChannelSettings::periodNotSet);
        char text[b1s::maxTextLength + 1u];
        assert(again.readText(ChannelSettings::Text::label, 8u, text) && std::strcmp(text, "Ignition") == 0);
        assert(!again.readText(ChannelSettings::Text::location, 8u, text));
    }
    // A perChannel record of 7 bytes (before on change) still loads.
    MemoryStorage storage(ChannelSettings::requiredBytes(2u));
    ChannelSettings::Override overrides[2u]{};
    slstm32::storage::RedundantBlobStore record(storage, 3u * 128u, 128u);
    const std::uint8_t old[7]{2u, 0xFFu, 0xFFu, 50u, 0u, 0u, 0u};
    assert(record.commit(old, sizeof old));
    ChannelSettings settings(storage, 0u, overrides);
    settings.load();
    assert(overrides[0].publish == 2u && overrides[0].period == 50u && overrides[0].onChange == ChannelSettings::notSet);
}

} // namespace

int main() {
    using namespace b1s;
    using SlQuantity = slstm32::sensors::Quantity;

    // Header: manufacturer 2046, reserved bits set, industry 4.
    std::uint8_t measurement[8];
    encodeMeasurement(measurement, Quantity::temperature, 3u, 299962);
    const std::uint8_t expected[8]{0xFEu, 0x9Fu, 1u, 3u, 0xBAu, 0x93u, 0x04u, 0x00u};
    assert(std::memcmp(measurement, expected, sizeof expected) == 0);
    assert(isB1s(measurement, sizeof measurement));
    const std::uint8_t other[2]{0x89u, 0x98u}; // another manufacturer
    assert(!isB1s(other, sizeof other));

    encodeMeasurement(measurement, Quantity::co2, 0u, notAvailable);
    assert(measurement[4] == 0xFFu && measurement[7] == 0x7Fu);

    // Conversions from slstm32 channel values (thousandths of the slstm32 unit).
    assert(toRaw(SlQuantity::temperature, 26812) == 299962);        // 26.812 °C -> 299.962 K
    assert(toRaw(SlQuantity::relativeHumidity, 41944) == 419440);   // 41.944 % -> 0.41944
    assert(toRaw(SlQuantity::pressure, 1013250) == 1013250);        // 1013.25 hPa -> 101325.0 Pa
    assert(toRaw(SlQuantity::concentration, 812000) == 812000);     // 812 ppm -> 812e-6
    assert(toRaw(SlQuantity::rotationalSpeed, 3000000) == 50000);   // 3000 rpm -> 50 Hz
    assert(toRaw(SlQuantity::angle, 90000) == 1570797);             // 90° -> 1.5708 rad
    assert(quantityOf(SlQuantity::concentration) == Quantity::co2);
    assert(toRaw(SlQuantity::state, 1000) == 1 && toRaw(SlQuantity::state, 0) == 0);
    assert(fromRaw(SlQuantity::state, 1) == 1000);
    assert(quantityOf(SlQuantity::state) == Quantity::state);
    assert(toRaw(SlQuantity::runTime, 1234500) == 4444200);         // 1234.5 h -> s
    assert(fromRaw(SlQuantity::runTime, 4444200) == 1234500);
    assert(quantityOf(SlQuantity::runTime) == Quantity::runTime);

    // Channel description with STRING_LAU texts.
    std::uint8_t description[channelDescriptionCapacity];
    ChannelDescription channel{};
    channel.channel = 2u;
    channel.kind = static_cast<std::uint8_t>(Quantity::temperature);
    channel.encoding = Encoding::standard;
    channel.standardInstance = 2u;
    channel.periodCentiseconds = 200u;
    channel.location = "inside.refrigerator";
    channel.label = "Fridge";
    const auto length = encodeChannelDescription(description, channel);
    assert(length == 8u + 2u + 19u + 2u + 6u);
    assert(description[4] == 0xE1u); // standard, not an output, has a standard PGN, no raw value, reserved bits set
    channel.noStandard = true;
    channel.encoding = Encoding::b1s;
    encodeChannelDescription(description, channel);
    assert(description[4] == 0xE8u);
    channel.raw = true;
    encodeChannelDescription(description, channel);
    assert(description[4] == 0xF8u);
    assert(description[5] == 2u && description[6] == 200u && description[7] == 0u);
    assert(description[8] == 21u && description[9] == 1u);
    assert(std::memcmp(description + 10, "inside.refrigerator", 19u) == 0);
    assert(description[29] == 8u && std::memcmp(description + 31, "Fridge", 6u) == 0);

    // Texts are truncated to 32 characters; the maximum fits the capacity.
    channel.location = "abcdefghijklmnopqrstuvwxyz0123456789";
    channel.label = channel.location;
    assert(encodeChannelDescription(description, channel) == channelDescriptionCapacity);

    // Registry categories.
    assert(standardCategory(130316u) == StandardCategory::display);
    assert(standardCategory(127250u) == StandardCategory::navigation);
    assert(standardCategory(12345u) == StandardCategory::unlisted);

    channelSettingsTests();

    std::puts("b1s tests passed");
    return 0;
}
