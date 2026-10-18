#include "node.hpp"
#include "b1s/messages.hpp"
#include "b1s/registry.hpp"
#include "can_mcu/image_header.hpp"
#include "n2k/bytes.hpp"
#include "n2k/name.hpp"
#include "version.hpp"

namespace boot {

namespace {

namespace image = can_mcu::image;

constexpr std::uint32_t pgnIsoAcknowledgement = 59392u;
constexpr std::uint32_t pgnIsoRequest = 59904u;
constexpr std::uint32_t pgnAddressClaim = 60928u;
constexpr std::uint32_t pgnHeartbeat = 126993u;
constexpr std::uint32_t pgnProductInformation = 126996u;
constexpr std::uint32_t claimSettleMs = 250u;
constexpr std::uint32_t heartbeatIntervalMs = 10000u;
constexpr std::uint32_t fastReceiveTimeoutMs = 750u;
constexpr std::uint8_t firstAddress = b1s::product::canMcuBootloader.preferredAddress;
constexpr std::uint8_t lastAddress = n2k::maximumAddress;
constexpr std::uint8_t replyBit = 0x80u;
constexpr std::size_t maxChunk = 200u;

bool reached(std::uint32_t now, std::uint32_t deadline) { return deadline != 0u && static_cast<std::int32_t>(now - deadline) >= 0; }

std::uint32_t get32(const std::uint8_t* data) {
    return data[0] | data[1] << 8u | data[2] << 16u | static_cast<std::uint32_t>(data[3]) << 24u;
}

const std::uint8_t* applicationAt(std::uint32_t offset) {
    return reinterpret_cast<const std::uint8_t*>(image::applicationBase + offset);
}

} // namespace

void Node::start() {
    std::uint8_t uid[12];
    hw::uniqueId(uid);
    const auto& product = b1s::product::canMcuBootloader;
    name_ = n2k::Name{n2k::uniqueNumberFrom(uid, sizeof uid), n2k::manufacturerCode, 0u, 0u, product.deviceFunction,
                      product.deviceClass, 0u, 4u, true}
                .value();
    // Spread devices over the bootloader range by their unique number.
    const auto unique = static_cast<std::uint32_t>(name_ & 0x1FFFFFu);
    address_ = static_cast<std::uint8_t>(firstAddress + unique % (lastAddress - firstAddress + 1u));
    claim(hw::millis());
}

void Node::claim(std::uint32_t now) {
    claimed_ = false;
    claimAt_ = now;
    sendClaim();
}

void Node::run() {
    hw::Frame frame{};
    while (hw::canReceive(frame)) handle(frame);
    const auto now = hw::millis();
    if (!claimed_ && now - claimAt_ >= claimSettleMs) {
        claimed_ = true;
        heartbeatAt_ = now;
        hw::logHex("bootloader: claimed address", address_);
    }
    if (claimed_ && reached(now, heartbeatAt_)) {
        heartbeatAt_ = now + heartbeatIntervalMs;
        sendHeartbeat();
    }
    // Yellow LED: two short flashes a second, fast while identifying or updating.
    const bool fast = updating_ || (identifyUntil_ != 0u && !reached(now, identifyUntil_));
    hw::setLed(fast ? (now / 100u) % 2u == 0u : (now % 500u) < 60u);
    if (reached(now, restartAt_)) {
        hw::log("bootloader: restart");
        hw::reset();
    }
}

void Node::handle(const hw::Frame& frame) {
    const auto id = n2k::decodeCanId(frame.id);
    if (id.pgn == pgnAddressClaim) {
        if (frame.length == 8u) handleClaim(id, frame.data);
        return;
    }
    if (id.destination != n2k::broadcastAddress && id.destination != address_) return;
    if (id.pgn == pgnIsoRequest && frame.length >= 3u) {
        handleRequest(id, frame.data);
    } else if (id.pgn == b1s::pgn::service && id.destination == address_) {
        receiveFast(id, frame.data, frame.length);
    } else if (id.pgn == b1s::pgn::command && id.destination == address_ && frame.length == 8u &&
               b1s::isB1s(frame.data, frame.length)) {
        handleCommand(id.source, frame.data);
    }
}

void Node::handleClaim(const n2k::CanId& id, const std::uint8_t* data) {
    if (id.source != address_) return;
    const auto theirs = n2k::bytes::get64(data);
    if (theirs == name_) return; // our own claim
    if (name_ < theirs) {
        sendClaim(); // ours wins: defend the address
        return;
    }
    address_ = address_ == lastAddress ? firstAddress : static_cast<std::uint8_t>(address_ + 1u);
    claim(hw::millis());
}

void Node::handleRequest(const n2k::CanId& id, const std::uint8_t* data) {
    const auto requested = n2k::bytes::get24(data);
    if (requested == pgnAddressClaim) {
        sendClaim();
    } else if (!claimed_) {
        return;
    } else if (requested == pgnProductInformation) {
        sendProductInformation();
    } else if (requested == pgnHeartbeat) {
        sendHeartbeat();
    } else if (id.destination == address_ && id.source <= n2k::maximumAddress) {
        std::uint8_t nack[8]{1u, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
        n2k::bytes::put24(nack + 5, requested);
        sendSingle(6u, pgnIsoAcknowledgement, id.source, nack, sizeof nack);
    }
}

void Node::receiveFast(const n2k::CanId& id, const std::uint8_t* data, std::uint8_t length) {
    if (length < 2u) return;
    const auto now = hw::millis();
    const auto sequence = static_cast<std::uint8_t>(data[0] >> 5u);
    const auto frame = static_cast<std::uint8_t>(data[0] & 0x1Fu);
    if (frame == 0u) {
        if (rxLength_ != 0u && id.source != rxSource_ && now - rxAt_ < fastReceiveTimeoutMs) return;
        rxLength_ = data[1] <= sizeof rxData_ ? data[1] : 0u;
        rxOffset_ = 0u;
        rxSource_ = id.source;
        rxSequence_ = sequence;
        rxNextFrame_ = 1u;
        rxAt_ = now;
        for (std::uint8_t index = 2u; index < length && rxOffset_ < rxLength_; ++index) rxData_[rxOffset_++] = data[index];
    } else {
        if (rxLength_ == 0u || id.source != rxSource_) return;
        if (sequence != rxSequence_ || frame != rxNextFrame_) {
            rxLength_ = 0u;
            return;
        }
        ++rxNextFrame_;
        rxAt_ = now;
        for (std::uint8_t index = 1u; index < length && rxOffset_ < rxLength_; ++index) rxData_[rxOffset_++] = data[index];
    }
    if (rxLength_ == 0u || rxOffset_ < rxLength_) return;
    const auto complete = rxLength_;
    rxLength_ = 0u;
    if (b1s::isB1s(rxData_, complete)) handleService(rxSource_, rxData_, complete);
}

void Node::handleService(std::uint8_t requester, const std::uint8_t* data, std::size_t length) {
    if (length < 4u || (data[2] & replyBit) != 0u) return;
    std::uint8_t reply[9]{};
    b1s::putHeader(reply);
    reply[2] = static_cast<std::uint8_t>(data[2] | replyBit);
    reply[3] = data[3];
    std::size_t replyLength = 5u;
    Status status = Status::unknown;
    switch (static_cast<b1s::Service>(data[2])) {
    case b1s::Service::firmwareBegin:
        status = length >= 4u + sizeof header_ ? begin(data + 4) : Status::invalidValue;
        reply[5] = static_cast<std::uint8_t>(maxChunk);
        replyLength = 6u;
        break;
    case b1s::Service::firmwareData:
        status = length >= 8u ? write(get32(data + 4), data + 8, length - 8u) : Status::invalidValue;
        n2k::bytes::put32(reply + 5, next_);
        replyLength = 9u;
        break;
    case b1s::Service::firmwareEnd:
        status = end();
        if (status == Status::done) restartAt_ = hw::millis() + 100u;
        break;
    default:
        break; // parameters and console are the application's
    }
    reply[4] = static_cast<std::uint8_t>(status);
    sendFast(6u, b1s::pgn::service, requester, reply, replyLength);
}

void Node::handleCommand(std::uint8_t requester, const std::uint8_t* data) {
    if ((data[2] & replyBit) != 0u) return;
    std::uint8_t result = 1u; // unknown command
    switch (static_cast<b1s::Command>(data[2])) {
    case b1s::Command::restart:
        result = 0u;
        restartAt_ = hw::millis() + 100u;
        break;
    case b1s::Command::enterBootloader:
        result = 0u; // already here
        break;
    case b1s::Command::identify: {
        const auto seconds = static_cast<std::uint32_t>(data[4] | data[5] << 8u);
        identifyUntil_ = seconds == 0u ? 0u : hw::millis() + seconds * 1000u;
        result = 0u;
        break;
    }
    default:
        break;
    }
    std::uint8_t reply[8]{};
    b1s::putHeader(reply);
    reply[2] = static_cast<std::uint8_t>(data[2] | replyBit);
    reply[3] = data[3];
    reply[4] = result;
    reply[5] = reply[6] = reply[7] = 0xFFu;
    sendSingle(3u, b1s::pgn::command, requester, reply, sizeof reply);
}

Node::Status Node::begin(const std::uint8_t* header) {
    const auto magic = get32(header);
    const auto version = static_cast<std::uint16_t>(header[4] | header[5] << 8u);
    const auto board = static_cast<std::uint16_t>(header[6] | header[7] << 8u);
    const auto size = get32(header + 12);
    if (magic != image::magic || version != image::headerVersion) return Status::invalidValue;
    if (board != image::boardCanMcuV1) return Status::wrongBoard;
    if (size > image::applicationSize) return Status::tooLarge;
    if (size % 8u != 0u || size < image::headerOffset + sizeof header_) return Status::invalidValue;
    // The first page goes first: an interrupted update never leaves an image
    // that could start.
    if (!hw::flashErasePage(image::applicationBase)) return Status::flashError;
    for (std::size_t index = 0u; index < sizeof header_; ++index) header_[index] = header[index];
    size_ = size;
    next_ = 0u;
    erasedPages_ = 1u;
    updating_ = true;
    hw::logHex("bootloader: update begins, bytes", size);
    return Status::done;
}

Node::Status Node::write(std::uint32_t offset, const std::uint8_t* data, std::size_t length) {
    if (!updating_) return Status::invalidValue;
    if (offset != next_) return Status::unexpectedOffset;
    if (length == 0u || length > maxChunk || length % 8u != 0u || offset + length > size_) return Status::invalidValue;
    const auto lastPage = (offset + length - 1u) / image::pageSize;
    while (erasedPages_ <= lastPage) {
        if (!hw::flashErasePage(image::applicationBase + erasedPages_ * image::pageSize)) return Status::flashError;
        ++erasedPages_;
    }
    if (!hw::flashProgram(image::applicationBase + offset, data, length)) return Status::flashError;
    next_ += static_cast<std::uint32_t>(length);
    return Status::done;
}

Node::Status Node::end() {
    if (!updating_ || next_ != size_) return Status::unexpectedOffset;
    updating_ = false;
    const auto* written = applicationAt(image::headerOffset);
    for (std::size_t index = 0u; index < sizeof header_; ++index) {
        if (written[index] != header_[index]) return Status::invalidValue;
    }
    const auto crc = image::imageCrc(applicationAt(0u), size_);
    if (crc != get32(header_ + image::crcOffset)) {
        hw::logHex("bootloader: CRC mismatch", crc);
        return Status::crcMismatch;
    }
    // A new image: it has a few starts to confirm itself.
    hw::setBackupRegister(8u, image::trialMagic << 16u);
    hw::setBackupRegister(9u, crc);
    hw::log("bootloader: update complete");
    return Status::done;
}

bool Node::sendSingle(std::uint8_t priority, std::uint32_t pgn, std::uint8_t destination, const std::uint8_t* data,
                      std::uint8_t length) {
    hw::Frame frame{};
    frame.id = n2k::encodeCanId({priority, pgn, address_, destination});
    frame.length = length;
    for (std::uint8_t index = 0u; index < length; ++index) frame.data[index] = data[index];
    return hw::canSend(frame);
}

bool Node::sendFast(std::uint8_t priority, std::uint32_t pgn, std::uint8_t destination, const std::uint8_t* data,
                    std::size_t length) {
    fastSequence_ = static_cast<std::uint8_t>((fastSequence_ + 1u) & 7u);
    hw::Frame frame{};
    frame.id = n2k::encodeCanId({priority, pgn, address_, destination});
    frame.length = 8u;
    std::size_t offset = 0u;
    for (std::uint8_t index = 0u; offset < length; ++index) {
        for (auto& byte : frame.data) byte = 0xFFu;
        frame.data[0] = static_cast<std::uint8_t>(fastSequence_ << 5u | index);
        std::size_t position = 1u;
        if (index == 0u) frame.data[position++] = static_cast<std::uint8_t>(length);
        while (position < 8u && offset < length) frame.data[position++] = data[offset++];
        if (!hw::canSend(frame)) return false;
    }
    return true;
}

void Node::sendClaim() {
    std::uint8_t data[8];
    n2k::bytes::put64(data, name_);
    sendSingle(6u, pgnAddressClaim, n2k::broadcastAddress, data, sizeof data);
}

void Node::sendProductInformation() {
    std::uint8_t data[134];
    n2k::bytes::put16(data, 2100u);
    n2k::bytes::put16(data + 2, b1s::product::canMcuBootloader.code);
    n2k::bytes::putString(data + 4, 32u, "Semantic Boat can-mcu bootloader");
    n2k::bytes::putString(data + 36, 32u, bootloaderVersion);
    n2k::bytes::putString(data + 68, 32u, reason_);
    char serial[25];
    std::uint8_t uid[12];
    hw::uniqueId(uid);
    for (std::size_t index = 0u; index < sizeof uid; ++index) {
        const auto byte = uid[sizeof uid - 1u - index];
        serial[2u * index] = "0123456789ABCDEF"[byte >> 4u];
        serial[2u * index + 1u] = "0123456789ABCDEF"[byte & 0xFu];
    }
    serial[24] = '\0';
    n2k::bytes::putString(data + 100, 32u, serial);
    data[132] = 0u;
    data[133] = 1u;
    sendFast(6u, pgnProductInformation, n2k::broadcastAddress, data, sizeof data);
}

void Node::sendHeartbeat() {
    std::uint8_t data[8]{};
    n2k::bytes::put16(data, static_cast<std::uint16_t>(heartbeatIntervalMs / 10u));
    data[2] = heartbeatSequence_;
    data[3] = static_cast<std::uint8_t>(0u | (3u << 2u) | (0u << 4u) | (3u << 6u));
    data[4] = data[5] = data[6] = data[7] = 0xFFu;
    heartbeatSequence_ = static_cast<std::uint8_t>((heartbeatSequence_ + 1u) % 253u);
    sendSingle(7u, pgnHeartbeat, n2k::broadcastAddress, data, sizeof data);
}

} // namespace boot
