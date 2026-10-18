#pragma once

#include <cstddef>
#include <cstdint>

// Firmware images of can-mcu devices and the flash layout shared by the
// bootloader and the applications (src/lib/b1s/FIRMWARE.md). Free of HAL
// and library dependencies: the bootloader, the applications, and host tests
// include it.
namespace can_mcu::image {

inline constexpr std::uint32_t flashBase = 0x08000000u;
inline constexpr std::uint32_t pageSize = 2048u;
inline constexpr std::uint32_t bootloaderSize = 16u * 1024u;
inline constexpr std::uint32_t applicationBase = flashBase + bootloaderSize;
inline constexpr std::uint32_t applicationSize = 224u * 1024u;
inline constexpr std::uint32_t headerOffset = 0x200u;

inline constexpr std::uint32_t magic = 0x21533142u; // "B1S!"
inline constexpr std::uint16_t headerVersion = 1u;
inline constexpr std::uint16_t boardCanMcuV1 = 1u; // b1s::Board::canMcuV1

// RTC backup registers 7 and 8 (0..6 hold the crash record): an application
// asks for the bootloader, and the start count of an unconfirmed image.
inline constexpr std::uint32_t bootRequestMagic = 0xB007B007u;
inline constexpr std::uint32_t trialMagic = 0xA5A5u;    // bits 16..31 of register 8
inline constexpr std::uint32_t maxTrialStarts = 3u;

struct Header {
    std::uint32_t magic;
    std::uint16_t headerVersion;
    std::uint16_t board;
    std::uint16_t productCode;
    std::uint16_t reserved;
    std::uint32_t imageSize; // filled in by stamp_image.py
    std::uint32_t imageCrc;  // filled in by stamp_image.py
    std::uint32_t version;   // major << 24 | minor << 16 | patch << 8 | development
    char versionText[40];
};
static_assert(sizeof(Header) == 64u, "the image header is 64 bytes");
inline constexpr std::size_t crcOffset = 16u; // offsetof(Header, imageCrc)

// The header an application defines in section .image_header; size and CRC
// are filled in after the link.
constexpr Header makeHeader(std::uint16_t productCode, std::uint8_t major, std::uint8_t minor, std::uint8_t patch,
                            bool development, const char* text) {
    Header header{magic, headerVersion, boardCanMcuV1, productCode, 0xFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                  static_cast<std::uint32_t>(major) << 24u | static_cast<std::uint32_t>(minor) << 16u |
                      static_cast<std::uint32_t>(patch) << 8u | (development ? 1u : 0u),
                  {}};
    for (std::size_t index = 0u; text && text[index] != '\0' && index + 1u < sizeof header.versionText; ++index) {
        header.versionText[index] = text[index];
    }
    return header;
}

// CRC-32 as zlib (reflected 0x04C11DB7), with a 16-entry table: small and
// fast enough for the bootloader (about 0.3 s for 100 KB at 8 MHz).
inline std::uint32_t crc32Update(std::uint32_t crc, const std::uint8_t* data, std::size_t size) {
    static constexpr std::uint32_t table[16]{
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
    };
    crc = ~crc;
    for (std::size_t index = 0u; index < size; ++index) {
        crc = table[(crc ^ data[index]) & 0x0Fu] ^ (crc >> 4u);
        crc = table[(crc ^ (data[index] >> 4u)) & 0x0Fu] ^ (crc >> 4u);
    }
    return ~crc;
}

// CRC of an image as stamp_image.py computes it: the four CRC bytes of the
// header count as 0xFF.
inline std::uint32_t imageCrc(const std::uint8_t* image, std::uint32_t size) {
    constexpr std::uint32_t crcAt = headerOffset + crcOffset;
    static constexpr std::uint8_t erased[4]{0xFFu, 0xFFu, 0xFFu, 0xFFu};
    auto crc = crc32Update(0u, image, crcAt);
    crc = crc32Update(crc, erased, sizeof erased);
    return crc32Update(crc, image + crcAt + 4u, size - crcAt - 4u);
}

} // namespace can_mcu::image
