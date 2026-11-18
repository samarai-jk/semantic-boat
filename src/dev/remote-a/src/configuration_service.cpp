#include "configuration_service.hpp"
#include "hal_eeprom.hpp"
#include <cstdio>
#include <cstring>

namespace remote_a {
namespace {

constexpr std::uint32_t transferTimeoutMs = 10000u;
constexpr std::uint32_t requestTimeoutMs = 10000u;
// After connecting, the active app is checked against the server's version
// once the subscriptions are through.
constexpr std::uint32_t checkDelayMs = 5000u;

void copyName(char* destination, std::size_t capacity, std::string_view source) {
    if (!destination || capacity == 0u) return;
    const auto length = source.size() < capacity - 1u ? source.size() : capacity - 1u;
    if (length != 0u) std::memcpy(destination, source.data(), length);
    destination[length] = '\0';
}

} // namespace

bool ConfigurationService::init() {
    if (!runtime_.millis || !packageBuffer_ || packageCapacity_ == 0u || !sourceBuffer_ ||
        sourceCapacity_ == 0u || !fallback_ || !actions_.reset) return false;
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

void ConfigurationService::indicateTransfer(bool active) {
    if (actions_.indicateTransfer) actions_.indicateTransfer(actions_.context, active);
}

void ConfigurationService::run() {
    const auto now = runtime_.millis();
    // Checked again for every gateway session (the server may have
    // restarted with edited apps).
    if (link_.gatewaySessions() != checkedSession_) {
        checkedSession_ = link_.gatewaySessions();
        connectedAt_ = 0u;
        checkSent_ = false;
    }
    if (!link_.connected()) {
        connectedAt_ = 0u;
    } else if (!checkSent_ && active_.valid && active_.nameLength != 0u) {
        if (connectedAt_ == 0u) connectedAt_ = now == 0u ? 1u : now;
        else if (static_cast<std::uint32_t>(now - connectedAt_) >= checkDelayMs) {
            checkSent_ = link_.sendConfigCheck(active_.nameView(), active_.sourceCrc32);
        }
    }
    if (requestWhenShown_ && !display_.updatePending()) {
        requestWhenShown_ = false;
        initialDroppedBytes_ = link_.droppedBytes();
        initialReceiveErrors_ = link_.receiveErrors();
        initialDecodeErrors_ = link_.decodeErrors();
        display_.setRenderingPaused(true);
        if (!link_.requestConfig(names_[requestIndex_].bytes)) {
            display_.setRenderingPaused(false);
            display_.dismissTransientModal();
            indicateTransfer(false);
            display_.showModal("LINK ERROR", "CONFIGURATION REQUEST FAILED.",
                               semantic_display::ModalSeverity::error);
        } else {
            configRequestPending_ = true;
            configRequestedAt_ = now;
        }
    }
    if (listRequestPending_ &&
        static_cast<std::uint32_t>(now - listRequestedAt_) >= requestTimeoutMs) {
        listRequestPending_ = false;
        listRequestTimedOut_ = true;
        display_.dismissTransientModal();
        indicateTransfer(false);
        display_.showModal("LINK ERROR", "APP LIST REQUEST TIMED OUT.",
                           semantic_display::ModalSeverity::error);
    }
    if (configRequestPending_ &&
        static_cast<std::uint32_t>(now - configRequestedAt_) >= requestTimeoutMs) {
        configRequestPending_ = false;
        configRequestTimedOut_ = true;
        display_.setRenderingPaused(false);
        display_.dismissTransientModal();
        indicateTransfer(false);
        display_.showModal("LINK ERROR", "APP REQUEST TIMED OUT.",
                           semantic_display::ModalSeverity::error);
    }
    if (!transferActive_) return;
    if (static_cast<std::uint32_t>(now - lastTransferAt_) < transferTimeoutMs) return;
    char message[96]{};
    std::snprintf(message, sizeof message, "CONFIGURATION TRANSFER TIMED OUT AFTER %lu OF %lu BYTES.",
                  static_cast<unsigned long>(receivedBytes_), static_cast<unsigned long>(expectedBytes_));
    transferError(message);
}

void ConfigurationService::requestList() {
    display_.dismissSelection();
    listRequestPending_ = false;
    listRequestTimedOut_ = false;
    if (!link_.connected()) {
        showCachedList();
        return;
    }
    if (!link_.requestConfigList()) {
        display_.showModal("LINK ERROR", "CONFIGURATION LIST REQUEST FAILED.",
                           semantic_display::ModalSeverity::error);
        return;
    }
    listRequestPending_ = true;
    listRequestedAt_ = runtime_.millis();
    display_.showTransientModal("APPS", "LOADING THE LIST OF APPS FROM THE SERVER.", "PLEASE WAIT");
    indicateTransfer(true);
}

void ConfigurationService::receiveListBegin(void* context, std::uint16_t listId,
                                             std::uint16_t count) {
    static_cast<ConfigurationService*>(context)->listBegin(listId, count);
}

void ConfigurationService::receiveListItem(void* context, std::uint16_t listId,
                                            std::uint16_t index,
                                            std::string_view name, bool hasCrc,
                                            std::uint32_t crc) {
    static_cast<ConfigurationService*>(context)->listItem(listId, index, name, hasCrc, crc);
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
    nameCount_ = 0u;
    listActive_ = true;
}

void ConfigurationService::listItem(std::uint16_t listId, std::uint16_t,
                                    std::string_view name, bool hasCrc, std::uint32_t crc) {
    // One entry stays free for the local app.
    if (!listActive_ || listId != listId_ || name.empty() || name == localDefaultName ||
        name.size() > maxNameBytes || nameCount_ + 1u >= names_.size()) return;
    names_[nameCount_] = {};
    copyName(names_[nameCount_].bytes, sizeof names_[nameCount_].bytes, name);
    names_[nameCount_].hasCrc = hasCrc;
    names_[nameCount_].crc = crc;
    ++nameCount_;
}

// Without the server: the apps in the EEPROM (they open without it).
__attribute__((optimize("Os"))) void ConfigurationService::showCachedList() {
    semantic_display::CachedConfig entries[semantic_display::maxConfigCacheSlots]{};
    std::size_t count{};
    nameCount_ = 0u;
    if (store_.list(entries, semantic_display::maxConfigCacheSlots, count)) {
        for (std::size_t index = 0u; index < count && nameCount_ + 1u < names_.size(); ++index) {
            const auto name = entries[index].nameView();
            if (name.empty() || name == localDefaultName) continue;
            names_[nameCount_] = {};
            copyName(names_[nameCount_].bytes, sizeof names_[nameCount_].bytes, name);
            ++nameCount_;
        }
    }
    arrangeList();
    (void)display_.showSelection("APPS (OFFLINE)",
        {this, nameCount_, &selectionItem, &selectionCompleted}, 0u, true);
}

// Opens a cached app (its version matches the server's, or the server is
// not there to ask): one EEPROM header write and a restart.
__attribute__((optimize("Os"))) bool ConfigurationService::switchToCached(std::string_view name, bool hasCrc, std::uint32_t crc) {
    semantic_display::CachedConfig cached{};
    if (!store_.find(name, cached) || (hasCrc && cached.sourceCrc32 != crc)) return false;
    // The check reads the package into the framebuffer's memory.
    display_.setRenderingPaused(true);
    clearEepromIoError();
    if (!store_.activate(cached, packageBuffer_, packageCapacity_)) {
        display_.setRenderingPaused(false);
        return false;
    }
    link_.unsubscribe();
    actions_.reset(actions_.context);
    return true;
}

void ConfigurationService::listEnd(std::uint16_t listId) {
    if (!listActive_ || listId != listId_) return;
    listActive_ = false;
    display_.dismissTransientModal();
    indicateTransfer(false);
    arrangeList();
    // Tiles like the page menu.
    (void)display_.showSelection("APPS",
        {this, nameCount_, &selectionItem, &selectionCompleted}, 0u, true);
}

// The apps in the order to show: recently opened first (the cache's
// generations), then the server's others in its order, the local app last;
// cached apps whose version matches get an asterisk.
__attribute__((optimize("Os"))) void ConfigurationService::arrangeList() {
    semantic_display::CachedConfig entries[semantic_display::maxConfigCacheSlots]{};
    std::size_t count{};
    if (!store_.list(entries, semantic_display::maxConfigCacheSlots, count)) count = 0u;
    // The local app, cached unless the firmware's own JSON has changed.
    auto& local = names_[nameCount_++];
    local = {};
    copyName(local.bytes, sizeof local.bytes, localDefaultName);
    local.local = true;
    local.hasCrc = true;
    const auto source = fallback_();
    local.crc = semantic_display::crc32(reinterpret_cast<const std::uint8_t*>(source.data()), source.size());
    for (std::size_t position = 0u; position < nameCount_; ++position) {
        auto& entry = names_[position];
        entry.rank = static_cast<std::uint16_t>(entry.local ? 0xffffu : 0x100u + position);
        for (std::size_t index = 0u; index < count; ++index) {
            if (entries[index].nameView() != std::string_view{entry.bytes}) continue;
            if (!entry.local) entry.rank = static_cast<std::uint16_t>(index);
            entry.cached = !entry.hasCrc || entries[index].sourceCrc32 == entry.crc;
            break;
        }
    }
    for (std::size_t position = 1u; position < nameCount_; ++position) {
        for (auto index = position; index != 0u && names_[index].rank < names_[index - 1u].rank; --index) {
            const auto swap = names_[index];
            names_[index] = names_[index - 1u];
            names_[index - 1u] = swap;
        }
    }
}

std::string_view ConfigurationService::baseName(std::size_t index) const {
    if (index >= nameCount_) return {};
    auto name = std::string_view{names_[index].bytes};
    const auto separator = name.find_last_of("/\\");
    if (separator != std::string_view::npos) name.remove_prefix(separator + 1u);
    const auto extension = name.find_last_of('.');
    return extension != std::string_view::npos && extension != 0u
        ? name.substr(0u, extension) : name;
}

std::string_view ConfigurationService::selectionItem(void* context, std::size_t index) {
    auto& self = *static_cast<ConfigurationService*>(context);
    const auto name = self.baseName(index);
    if (name.empty() || !self.names_[index].cached) return name;
    // The display copies the text at once, so one buffer serves all tiles.
    copyName(self.label_, sizeof self.label_ - 1u, name);
    const auto length = std::char_traits<char>::length(self.label_);
    self.label_[length] = '*';
    self.label_[length + 1u] = '\0';
    return {self.label_, length + 1u};
}

__attribute__((optimize("Os"))) void ConfigurationService::selectionCompleted(
    void* context, semantic_display::SelectionResult result, std::size_t index) {
    auto& self = *static_cast<ConfigurationService*>(context);
    if (result == semantic_display::SelectionResult::cancelled) return;
    if (index >= self.nameCount_) return;
    const auto& entry = self.names_[index];
    if (entry.local) {
        if (!self.switchToCached(localDefaultName, entry.hasCrc, entry.crc)) self.loadDefault();
        return;
    }
    if (self.switchToCached(entry.bytes, entry.hasCrc, entry.crc)) return;
    self.configRequestPending_ = false;
    self.configRequestTimedOut_ = false;
    if (index >= self.nameCount_ || !self.link_.connected()) {
        self.display_.showModal("LINK ERROR", "CONFIGURATION REQUEST FAILED.",
                                semantic_display::ModalSeverity::error);
        return;
    }
    // Show what happens first; run() sends the request once it is on the panel.
    char message[128]{};
    const auto name = self.baseName(index);
    std::snprintf(message, sizeof message, "RECEIVING %.*s. THE DISPLAY RESTARTS WHEN IT IS INSTALLED.",
                  static_cast<int>(name.size()), name.data());
    for (auto* cursor = message; *cursor; ++cursor) {
        if (*cursor >= 'a' && *cursor <= 'z') *cursor = static_cast<char>(*cursor - 'a' + 'A');
    }
    self.display_.showTransientModal("INSTALLING APP", message, "PLEASE WAIT");
    self.indicateTransfer(true);
    self.requestIndex_ = index;
    self.requestWhenShown_ = true;
}

__attribute__((optimize("Os"))) void ConfigurationService::loadDefault() {
    display_.setRenderingPaused(true);
    link_.unsubscribe();
    requestedTransfer_ = true;
    if (!compileAndStore(fallback_(), localDefaultName)) return;
    actions_.reset(actions_.context);
}

void ConfigurationService::configBegin(std::uint16_t transferId, std::uint32_t size,
                                       std::uint32_t crc, std::string_view name) {
    if (!configRequestPending_) {
        initialDroppedBytes_ = link_.droppedBytes();
        initialReceiveErrors_ = link_.receiveErrors();
        initialDecodeErrors_ = link_.decodeErrors();
    }
    requestedTransfer_ = configRequestPending_;
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
    failedOffset_ = 0u;
    failedExpectedOffset_ = 0u;
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
    if (!data || offset > expectedBytes_ || size > expectedBytes_ - offset) {
        failedOffset_ = offset;
        failedExpectedOffset_ = static_cast<std::uint32_t>(receivedBytes_);
        transferFailed_ = true;
        return;
    }
    if (offset < receivedBytes_) {
        // Repeated frames are harmless on an asynchronous link as long as the
        // bytes are identical and entirely within the accepted prefix.
        if (size <= receivedBytes_ - offset &&
            std::memcmp(sourceBuffer_ + offset, data, size) == 0) {
            lastTransferAt_ = runtime_.millis();
            return;
        }
        failedOffset_ = offset;
        failedExpectedOffset_ = static_cast<std::uint32_t>(receivedBytes_);
        transferFailed_ = true;
        return;
    }
    if (offset != receivedBytes_) {
        failedOffset_ = offset;
        failedExpectedOffset_ = static_cast<std::uint32_t>(receivedBytes_);
        transferFailed_ = true;
        return;
    }
    std::memcpy(sourceBuffer_ + receivedBytes_, data, size);
    receivedBytes_ += size;
    lastTransferAt_ = runtime_.millis();
}

__attribute__((optimize("Os"))) bool ConfigurationService::compileAndStore(std::string_view source, std::string_view name) {
    const auto result = compiler_.compile(source, packageBuffer_, packageCapacity_);
    if (!result) {
        transferError(result.message);
        return false;
    }
    semantic_display::CachedConfig committed{};
    clearEepromIoError();
    if (!store_.store(name, packageBuffer_, result.packageSize, committed)) {
        transferError(eepromIoErrorMessage());
        return false;
    }
    return true;
}

void ConfigurationService::transferError(const char* message) {
    transferActive_ = false;
    indicateTransfer(false);
    display_.setRenderingPaused(false);
    // An update the server sent by itself (the check): the running app
    // stays; checked (and sent) again a few times before the error shows.
    if (!requestedTransfer_ && backgroundFailures_ < 3u) {
        ++backgroundFailures_;
        checkSent_ = false;
        connectedAt_ = 0u;
        return;
    }
    display_.dismissTransientModal();
    display_.showModal("CONFIG ERROR", message,
                       semantic_display::ModalSeverity::error);
}

__attribute__((optimize("Os"))) void ConfigurationService::configEnd(std::uint16_t transferId) {
    if (!transferActive_ || transferId != transferId_) return;
    transferActive_ = false;
    if (transferFailed_) {
        char message[160]{};
        std::snprintf(message, sizeof message,
                  "CONFIG CHUNK AT %lu; EXPECTED %lu. DROPPED %lu; UART ERRORS %lu; FLAGS %lu; DECODE %lu.",
                      static_cast<unsigned long>(failedOffset_),
                  static_cast<unsigned long>(failedExpectedOffset_),
                  static_cast<unsigned long>(link_.droppedBytes() - initialDroppedBytes_),
                  static_cast<unsigned long>(link_.receiveErrors() - initialReceiveErrors_),
                  static_cast<unsigned long>(link_.lastReceiveError()),
                  static_cast<unsigned long>(link_.decodeErrors() - initialDecodeErrors_));
        transferError(message);
        return;
    }
    if (receivedBytes_ != expectedBytes_) {
        char message[96]{};
        std::snprintf(message, sizeof message,
                      "RECEIVED %lu OF %lu CONFIG BYTES.",
                      static_cast<unsigned long>(receivedBytes_),
                      static_cast<unsigned long>(expectedBytes_));
        transferError(message);
        return;
    }
    const auto actualCrc = semantic_display::crc32(
        reinterpret_cast<const std::uint8_t*>(sourceBuffer_), receivedBytes_);
    if (actualCrc != expectedCrc_) {
        transferError("CONFIGURATION CHECKSUM FAILED.");
        return;
    }

    // The download is complete. Stop server data before the deliberately
    // synchronous compile/EEPROM phase; on failure the link service will
    // publish the active subscriptions again on its next run.
    link_.unsubscribe();
    if (!compileAndStore({sourceBuffer_, receivedBytes_}, transferName_.bytes)) return;
    display_.dismissTransientModal();
    actions_.reset(actions_.context);
}

} // namespace remote_a
