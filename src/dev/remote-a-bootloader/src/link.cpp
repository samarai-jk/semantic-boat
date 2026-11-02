#include "link.hpp"
#include "b1s/firmware_image.hpp"
#include "hardware.hpp"
#include "version.hpp"

namespace boot {

namespace {

namespace image = b1s::image;
namespace link = semantic_link;

constexpr std::uint32_t restartDelayMs = 200u; // after the reply has left
constexpr auto board = static_cast<std::uint16_t>(b1s::Board::remoteA);

std::uint32_t read32(const std::uint8_t* data) {
    return data[0] | data[1] << 8u | data[2] << 16u | static_cast<std::uint32_t>(data[3]) << 24u;
}

const std::uint8_t* applicationAt(std::uint32_t offset) {
    return reinterpret_cast<const std::uint8_t*>(image::applicationBase + offset);
}

} // namespace

void Link::start() {
    endpoint_.setAddress(address_);
    hw::logHex("bootloader: RS485 address", address_);
}

void Link::run() {
    std::uint8_t byte = 0u;
    while (hw::rs485Read(byte)) {
        link::MessageView message{};
        if (decoder_.push(byte, message) != link::DecodeResult::message) continue;
        const auto now = hw::millis();
        if (!endpoint_.request(message, now, now)) continue;
        ++requests_;
        if (endpoint_.duplicate()) ++duplicates_;
        if (!endpoint_.duplicate()) {
            endpoint_.clearQueue();
            handle(message);
        }
        std::uint8_t reply[link::maxEncodedFrameSize];
        const auto size = endpoint_.reply(hw::millis(), reply, sizeof reply);
        if (size != 0u) hw::rs485Write(reply, size);
        else ++late_;
        decoder_.reset();
    }
    if (restartPending_ && static_cast<std::int32_t>(hw::millis() - restartAt_) >= 0) hw::reset();
}

void Link::reportStatus(Result result) {
    char text[96] = "bootloader ";
    std::size_t length = 0u;
    while (text[length] != '\0') ++length;
    for (const char* cursor = bootloaderVersion; *cursor != '\0' && length < 30u; ++cursor) text[length++] = *cursor;
    text[length++] = ':';
    text[length++] = ' ';
    for (const char* cursor = reason_; *cursor != '\0' && length + 1u < sizeof text; ++cursor) text[length++] = *cursor;
    text[length] = '\0';
    // Counters: requests, retries, late, decode errors.
    const std::uint32_t counters[]{requests_, duplicates_, late_, decoder_.errorCount()};
    for (const auto counter : counters) {
        char digits[10];
        std::size_t count = 0u;
        auto value = counter;
        do { digits[count++] = static_cast<char>('0' + value % 10u); value /= 10u; } while (value != 0u && count < sizeof digits);
        if (length + count + 2u >= sizeof text) break;
        text[length++] = ' ';
        while (count != 0u) text[length++] = digits[--count];
    }
    text[length] = '\0';
    std::uint8_t payload[link::maxPayloadSize];
    const auto size = link::encodeFirmwareStatus(
        payload, sizeof payload, result, link::FirmwareMode::bootloader, board, next_,
        read32(applicationAt(image::headerOffset + image::crcOffset)), bootloaderVersionNumber, 0u, text);
    (void)endpoint_.enqueue(link::MessageType::firmwareStatus, payload, size);
}

void Link::handle(const link::MessageView& message) {
    const auto* payload = message.payload;
    const auto size = message.payloadSize;
    switch (message.type) {
    case link::MessageType::firmwareQuery:
    case link::MessageType::firmwareEnterBootloader:
        reportStatus(Result::done);
        break;
    case link::MessageType::firmwareBegin:
        reportStatus(begin(payload, size));
        break;
    case link::MessageType::firmwareData:
        reportStatus(write(payload, size));
        break;
    case link::MessageType::firmwareEnd: {
        const auto result = end();
        reportStatus(result);
        if (result == Result::done) {
            restartPending_ = true;
            restartAt_ = hw::millis() + restartDelayMs;
        }
        break;
    }
    case link::MessageType::firmwareRestart:
        reportStatus(Result::done);
        restartPending_ = true;
        restartAt_ = hw::millis() + restartDelayMs;
        break;
    default:
        break; // Idle
    }
}

Link::Result Link::begin(const std::uint8_t* header, std::size_t length) {
    if (length != sizeof header_) return Result::invalidValue;
    const auto magic = read32(header);
    const auto version = static_cast<std::uint16_t>(header[4] | header[5] << 8u);
    const auto imageBoard = static_cast<std::uint16_t>(header[6] | header[7] << 8u);
    const auto size = read32(header + 12);
    if (magic != image::magic || version != image::headerVersion) return Result::invalidValue;
    if (imageBoard != board) return Result::wrongBoard;
    if (size > image::applicationSize) return Result::tooLarge;
    if (size % 8u != 0u || size < image::headerOffset + sizeof header_) return Result::invalidValue;
    // The first page goes first: an interrupted update never leaves an image
    // that could start.
    if (!hw::flashErasePage(image::applicationBase)) return Result::flashError;
    for (std::size_t index = 0u; index < sizeof header_; ++index) header_[index] = header[index];
    size_ = size;
    next_ = 0u;
    erasedPages_ = 1u;
    crc_ = 0u;
    updating_ = true;
    hw::logHex("bootloader: update begins, bytes", size);
    return Result::done;
}

Link::Result Link::write(const std::uint8_t* payload, std::size_t length) {
    if (!updating_ || length < 4u) return Result::invalidValue;
    const auto offset = read32(payload);
    const auto* data = payload + 4u;
    length -= 4u;
    if (offset != next_) return Result::unexpectedOffset;
    if (length == 0u || length > link::firmwareChunkBytes || length % 8u != 0u || offset + length > size_) {
        return Result::invalidValue;
    }
    const auto lastPage = (offset + length - 1u) / image::pageSize;
    while (erasedPages_ <= lastPage) {
        if (!hw::flashErasePage(image::applicationBase + erasedPages_ * image::pageSize)) return Result::flashError;
        ++erasedPages_;
    }
    if (!hw::flashProgram(image::applicationBase + offset, data, length)) return Result::flashError;
    // The image CRC as it arrives (flashProgram has read every byte back), so
    // firmwareEnd can answer within the reply window; the CRC field counts as 0xFF.
    constexpr std::uint32_t crcAt = image::headerOffset + image::crcOffset;
    static constexpr std::uint8_t erased[4]{0xFFu, 0xFFu, 0xFFu, 0xFFu};
    for (std::size_t index = 0u; index < length;) {
        const auto position = offset + index;
        if (position >= crcAt && position < crcAt + 4u) {
            crc_ = image::crc32Update(crc_, erased, 1u);
            ++index;
            continue;
        }
        auto run = length - index;
        if (position < crcAt && position + run > crcAt) run = crcAt - position;
        crc_ = image::crc32Update(crc_, data + index, run);
        index += run;
    }
    next_ += static_cast<std::uint32_t>(length);
    return Result::done;
}

Link::Result Link::end() {
    if (!updating_ || next_ != size_) return Result::unexpectedOffset;
    updating_ = false;
    const auto* written = applicationAt(image::headerOffset);
    for (std::size_t index = 0u; index < sizeof header_; ++index) {
        if (written[index] != header_[index]) return Result::invalidValue;
    }
    const auto crc = crc_;
    if (crc != read32(header_ + image::crcOffset)) {
        hw::logHex("bootloader: CRC mismatch", crc);
        return Result::crcMismatch;
    }
    // A new image: it has a few starts to confirm itself.
    hw::setBackupRegister(8u, image::trialMagic << 16u);
    hw::setBackupRegister(9u, crc);
    hw::log("bootloader: update complete");
    return Result::done;
}

} // namespace boot
