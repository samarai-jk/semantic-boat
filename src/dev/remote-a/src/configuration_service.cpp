#include "configuration_service.hpp"
#include "hal_eeprom.hpp"
#include <cstring>

namespace remote_a {
namespace {

constexpr std::uint32_t transferTimeoutMs = 10000u;
constexpr std::uint32_t requestTimeoutMs = 3000u;
constexpr char localDefaultName[] = "DEFAULT (LOCAL)";

void copyName(char* destination, std::size_t capacity, std::string_view source) {
    if (!destination || capacity == 0u) return;
    const auto length = source.size() < capacity - 1u ? source.size() : capacity - 1u;
    if (length != 0u) std::memcpy(destination, source.data(), length);
    destination[length] = '\0';
}

} // namespace

bool ConfigurationService::init() {
    if (!runtime_.millis || !packageBuffer_ || packageCapacity_ == 0u || !sourceBuffer_ ||
        sourceCapacity_ == 0u || !fallback_) return false;
    link_.setConfigReceiver({
        this,
        &receiveListBegin,
        &receiveListItem,
        &receiveListEnd,
        &receiveConfigBegin,
        &receiveConfigChunk,
        &receiveConfigEnd,
    });
    return true;
}

void ConfigurationService::run() {
    const auto now = runtime_.millis();
    if (listRequestPending_ &&
        static_cast<std::uint32_t>(now - listRequestedAt_) >= requestTimeoutMs) {
        listRequestPending_ = false;
        listRequestTimedOut_ = true;
        display_.showModal("LINK ERROR", "APP LIST REQUEST TIMED OUT.",
                           semantic_display::ModalSeverity::error);
    }
    if (configRequestPending_ &&
        static_cast<std::uint32_t>(now - configRequestedAt_) >= requestTimeoutMs) {
        configRequestPending_ = false;
        configRequestTimedOut_ = true;
        display_.showModal("LINK ERROR", "APP REQUEST TIMED OUT.",
                           semantic_display::ModalSeverity::error);
    }
    if (!transferActive_) return;
    if (static_cast<std::uint32_t>(now - lastTransferAt_) < transferTimeoutMs) return;
    transferError("CONFIGURATION TRANSFER TIMED OUT.");
}

void ConfigurationService::requestList() {
    display_.dismissSelection();
    listRequestPending_ = false;
    listRequestTimedOut_ = false;
    if (!link_.connected()) {
        display_.showModal("LINK ERROR", "SERVER IS NOT CONNECTED.",
                           semantic_display::ModalSeverity::error);
        return;
    }
    if (!link_.requestConfigList()) {
        display_.showModal("LINK ERROR", "CONFIGURATION LIST REQUEST FAILED.",
                           semantic_display::ModalSeverity::error);
        return;
    }
    listRequestPending_ = true;
    listRequestedAt_ = runtime_.millis();
}

void ConfigurationService::receiveListBegin(void* context, std::uint16_t listId,
                                             std::uint16_t count) {
    static_cast<ConfigurationService*>(context)->listBegin(listId, count);
}

void ConfigurationService::receiveListItem(void* context, std::uint16_t listId,
                                            std::uint16_t index,
                                            std::string_view name) {
    static_cast<ConfigurationService*>(context)->listItem(listId, index, name);
}

void ConfigurationService::receiveListEnd(void* context, std::uint16_t listId) {
    static_cast<ConfigurationService*>(context)->listEnd(listId);
}

void ConfigurationService::receiveConfigBegin(void* context, std::uint16_t transferId,
                                               std::uint32_t size, std::uint32_t crc,
                                               std::string_view name) {
    static_cast<ConfigurationService*>(context)->configBegin(transferId, size, crc, name);
}

void ConfigurationService::receiveConfigChunk(void* context, std::uint16_t transferId,
                                               std::uint32_t offset,
                                               const std::uint8_t* data,
                                               std::size_t size) {
    static_cast<ConfigurationService*>(context)->configChunk(transferId, offset, data, size);
}

void ConfigurationService::receiveConfigEnd(void* context, std::uint16_t transferId) {
    static_cast<ConfigurationService*>(context)->configEnd(transferId);
}

void ConfigurationService::listBegin(std::uint16_t listId, std::uint16_t) {
    listRequestPending_ = false;
    if (listRequestTimedOut_) {
        display_.dismissModal();
        listRequestTimedOut_ = false;
    }
    display_.dismissSelection();
    listId_ = listId;
    copyName(names_[0].bytes, sizeof names_[0].bytes, localDefaultName);
    nameCount_ = 1u;
    listActive_ = true;
}

void ConfigurationService::listItem(std::uint16_t listId, std::uint16_t,
                                    std::string_view name) {
    if (!listActive_ || listId != listId_ || name.empty() ||
        name.size() > maxNameBytes || nameCount_ == names_.size()) return;
    copyName(names_[nameCount_].bytes, sizeof names_[nameCount_].bytes, name);
    ++nameCount_;
}

void ConfigurationService::listEnd(std::uint16_t listId) {
    if (!listActive_ || listId != listId_) return;
    listActive_ = false;
    (void)display_.showSelection("APPS",
        {this, nameCount_, &selectionItem, &selectionCompleted});
}

std::string_view ConfigurationService::selectionItem(void* context, std::size_t index) {
    auto& self = *static_cast<ConfigurationService*>(context);
    if (index >= self.nameCount_) return {};

    auto name = std::string_view{self.names_[index].bytes};
    const auto separator = name.find_last_of("/\\");
    if (separator != std::string_view::npos) name.remove_prefix(separator + 1u);
    const auto extension = name.find_last_of('.');
    return extension != std::string_view::npos && extension != 0u
        ? name.substr(0u, extension) : name;
}

void ConfigurationService::selectionCompleted(
    void* context, semantic_display::SelectionResult result, std::size_t index) {
    auto& self = *static_cast<ConfigurationService*>(context);
    if (result == semantic_display::SelectionResult::cancelled) return;
    if (index == 0u) {
        self.loadDefault();
        return;
    }
    self.configRequestPending_ = false;
    self.configRequestTimedOut_ = false;
    if (index >= self.nameCount_ || !self.link_.connected() ||
        !self.link_.requestConfig(self.names_[index].bytes)) {
        self.display_.showModal("LINK ERROR", "CONFIGURATION REQUEST FAILED.",
                                semantic_display::ModalSeverity::error);
        return;
    }
    self.configRequestPending_ = true;
    self.configRequestedAt_ = self.runtime_.millis();
}

void ConfigurationService::loadDefault() {
    display_.setRenderingPaused(true);
    const auto source = fallback_();
    const auto result = compiler_.compile(source, packageBuffer_, packageCapacity_);
    if (!result) {
        transferError(result.message);
        return;
    }
    if (!activate(result.packageSize)) {
        transferError("DEFAULT CONFIGURATION COULD NOT BE ACTIVATED.");
        return;
    }

    semantic_display::StoredConfigInfo committed{};
    clearEepromIoError();
    const auto persisted = store_.commit(packageBuffer_, result.packageSize, committed);
    display_.setRenderingPaused(false);
    if (!persisted) {
        display_.showModal("STORAGE ERROR", eepromIoErrorMessage(),
                           semantic_display::ModalSeverity::error);
    }
}

void ConfigurationService::configBegin(std::uint16_t transferId, std::uint32_t size,
                                       std::uint32_t crc, std::string_view name) {
    configRequestPending_ = false;
    if (configRequestTimedOut_) {
        display_.dismissModal();
        configRequestTimedOut_ = false;
    }
    display_.dismissSelection();
    display_.setRenderingPaused(true);
    transferId_ = transferId;
    expectedBytes_ = size;
    expectedCrc_ = crc;
    receivedBytes_ = 0u;
    transferActive_ = true;
    lastTransferAt_ = runtime_.millis();
    transferFailed_ = size == 0u || size > sourceCapacity_ || name.empty() ||
        name.size() > maxNameBytes;
    copyName(transferName_.bytes, sizeof transferName_.bytes, name);
    if (transferFailed_) {
        transferError(size > sourceCapacity_ ? "CONFIGURATION IS TOO LARGE."
                                             : "INVALID CONFIGURATION TRANSFER.");
        return;
    }
}

void ConfigurationService::configChunk(std::uint16_t transferId, std::uint32_t offset,
                                       const std::uint8_t* data, std::size_t size) {
    if (!transferActive_ || transferId != transferId_ || transferFailed_) return;
    if (!data || offset != receivedBytes_ || size > expectedBytes_ - receivedBytes_) {
        transferFailed_ = true;
        return;
    }
    std::memcpy(sourceBuffer_ + receivedBytes_, data, size);
    receivedBytes_ += size;
    lastTransferAt_ = runtime_.millis();
}

bool ConfigurationService::activate(std::size_t packageSize) {
    semantic_display::PackageView candidate{packageBuffer_, packageSize};
    if (!candidate.valid()) return false;
    package_ = candidate;
    if (!display_.setPackage(package_)) return false;
    link_.packageChanged();
    return true;
}

void ConfigurationService::restoreFallback() {
    semantic_display::StoredConfigInfo stored{};
    if (store_.load(packageBuffer_, packageCapacity_, stored) &&
        activate(stored.packageSize)) return;
    const auto source = fallback_();
    const auto result = compiler_.compile(source, packageBuffer_, packageCapacity_);
    if (result) (void)activate(result.packageSize);
}

void ConfigurationService::transferError(const char* message) {
    transferActive_ = false;
    display_.setRenderingPaused(false);
    display_.dismissTransientModal();
    display_.showModal("CONFIG ERROR", message,
                       semantic_display::ModalSeverity::error);
}

void ConfigurationService::configEnd(std::uint16_t transferId) {
    if (!transferActive_ || transferId != transferId_) return;
    transferActive_ = false;
    if (transferFailed_ || receivedBytes_ != expectedBytes_) {
        transferError("CONFIGURATION TRANSFER WAS INCOMPLETE.");
        return;
    }
    const auto actualCrc = semantic_display::crc32(
        reinterpret_cast<const std::uint8_t*>(sourceBuffer_), receivedBytes_);
    if (actualCrc != expectedCrc_) {
        transferError("CONFIGURATION CHECKSUM FAILED.");
        return;
    }

    const auto result = compiler_.compile(
        {sourceBuffer_, receivedBytes_}, packageBuffer_, packageCapacity_);
    if (!result) {
        restoreFallback();
        transferError(result.message);
        return;
    }
    if (!activate(result.packageSize)) {
        restoreFallback();
        transferError("COMPILED CONFIGURATION COULD NOT BE ACTIVATED.");
        return;
    }

    semantic_display::StoredConfigInfo committed{};
    clearEepromIoError();
    const auto persisted = store_.commit(packageBuffer_, result.packageSize, committed);
    display_.setRenderingPaused(false);
    display_.dismissTransientModal();
    if (!persisted) {
        display_.showModal("STORAGE ERROR", eepromIoErrorMessage(),
                           semantic_display::ModalSeverity::error);
    }
}

} // namespace remote_a
