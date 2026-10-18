// Host tests of the B1S encoders and unit conversions. Build and run (no
// firmware toolchain needed), from the repository root:
//   g++ -std=c++17 -Wall -Wextra -Isrc/lib/b1s/include -Isrc/lib/n2k/include
//       -Isrc/lib/slstm32/include src/lib/b1s/tests/b1s_tests.cpp src/lib/b1s/src/quantity.cpp
//       -o build/b1s_tests && build/b1s_tests
#include <cassert>
#include <cstdio>
#include <cstring>
#include "b1s/messages.hpp"
#include "b1s/quantity.hpp"

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
    assert(description[4] == 0xF9u); // standard, not an output, reserved bits set
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

    std::puts("b1s tests passed");
    return 0;
}
