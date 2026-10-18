// Host tests of the firmware image header and CRC (src/lib/b1s/FIRMWARE.md).
// From the repository root:
//   g++ -std=c++17 -Wall -Wextra -Isrc/dev/can-mcu/common/include
//       src/dev/can-mcu/common/tests/image_tests.cpp -o build/image_tests
//   build/image_tests [stamped image .bin ...]
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "can_mcu/image_header.hpp"

int main(int argc, char** argv) {
    namespace image = can_mcu::image;
    const char check[] = "123456789";
    assert(image::crc32Update(0u, reinterpret_cast<const std::uint8_t*>(check), 9u) == 0xCBF43926u); // CRC-32 check value

    constexpr auto header = image::makeHeader(1u, 0u, 1u, 0u, true, "0.1.0-dev");
    static_assert(header.magic == image::magic && header.board == image::boardCanMcuV1, "header fields");
    static_assert(header.version == 0x00010001u, "version packing");
    assert(std::strcmp(header.versionText, "0.1.0-dev") == 0);
    static_assert(image::applicationBase == 0x08004000u && image::applicationBase + image::applicationSize == 0x0803C000u,
                  "flash layout");

    // Stamped images: the CRC in the header is the CRC the bootloader computes.
    for (int index = 1; index < argc; ++index) {
        std::ifstream file(argv[index], std::ios::binary);
        const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        image::Header stamped{};
        std::memcpy(&stamped, data.data() + image::headerOffset, sizeof stamped);
        assert(stamped.magic == image::magic);
        assert(stamped.imageSize == data.size());
        assert(image::imageCrc(data.data(), stamped.imageSize) == stamped.imageCrc);
        std::printf("%s: %u bytes, CRC %08X matches\n", argv[index], static_cast<unsigned>(stamped.imageSize),
                    static_cast<unsigned>(stamped.imageCrc));
    }
    std::puts("image tests passed");
    return 0;
}
