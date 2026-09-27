#include "semantic_display/compiler.hpp"
#include "semantic_display/storage.hpp"
#include "slstm32/storage/redundant_blob_store.hpp"
#include <array>
#include <cassert>
#include <cstring>

namespace {

class MemoryStorage final : public slstm32::storage::ByteStorage {
public:
    MemoryStorage() { bytes.fill(0xffu); }
    std::size_t capacity() const override { return bytes.size(); }
    bool read(std::uint32_t address, std::uint8_t* output, std::size_t size) override {
        if (address > bytes.size() || size > bytes.size() - address) return false;
        std::memcpy(output, bytes.data() + address, size);
        return true;
    }
    bool write(std::uint32_t address, const std::uint8_t* input, std::size_t size) override {
        if (failWrites || address > bytes.size() || size > bytes.size() - address) return false;
        std::memcpy(bytes.data() + address, input, size);
        return true;
    }
    std::array<std::uint8_t, 32768> bytes{};
    bool failWrites{};
};

constexpr auto configA = R"json({"schema":"semantic-display/v1","sections":[{
  "id":"a","pages":[{"id":"p","grid":{"columns":1,"rows":1},
  "widgets":[{"type":"text","cell":{"column":0,"row":0},"text":"A"}]}]
}]})json";
constexpr auto configB = R"json({"schema":"semantic-display/v1","sections":[{
  "id":"b","pages":[{"id":"p","grid":{"columns":1,"rows":1},
  "widgets":[{"type":"text","cell":{"column":0,"row":0},"text":"B"}]}]
}]})json";

} // namespace

int main() {
    MemoryStorage memory;
    semantic_display::ConfigStore store{memory};
    assert(store.layout().slotBytes() == 16256u);
    assert(store.layout().maxPackageBytes() == 16192u);
    assert(store.layout().settingsOffset() == 32512u);

    std::array<std::uint8_t, 1024> packageA{}, packageB{}, loaded{};
    semantic_display::ConfigCompiler compiler;
    const auto compiledA = compiler.compile(configA, packageA.data(), packageA.size());
    const auto compiledB = compiler.compile(configB, packageB.data(), packageB.size());
    assert(compiledA && compiledB);

    semantic_display::StoredConfigInfo info{};
    assert(store.commit(packageA.data(), compiledA.packageSize, info));
    assert(info.valid && info.slot == 0u && info.generation == 1u);
    semantic_display::StoredConfigInfo loadedInfo{};
    assert(store.load(loaded.data(), loaded.size(), loadedInfo));
    assert(loadedInfo.generation == 1u);
    assert((semantic_display::PackageView{loaded.data(), loadedInfo.packageSize}.valid()));

    assert(store.commit(packageB.data(), compiledB.packageSize, info));
    assert(info.slot == 1u && info.generation == 2u);
    assert(store.load(loaded.data(), loaded.size(), loadedInfo));
    assert(loadedInfo.generation == 2u);
    assert((semantic_display::PackageView{loaded.data(), loadedInfo.packageSize}.info().sourceCrc32 ==
            semantic_display::PackageView{packageB.data(), compiledB.packageSize}.info().sourceCrc32));

    // Corrupt the newest slot. The older committed generation remains usable.
    memory.bytes[store.layout().slotOffset(1u) + store.layout().slotHeaderBytes + 10u] ^= 0x80u;
    assert(store.load(loaded.data(), loaded.size(), loadedInfo));
    assert(loadedInfo.slot == 0u && loadedInfo.generation == 1u);

    slstm32::storage::RedundantBlobStore settings{
        memory, static_cast<std::uint32_t>(store.layout().settingsOffset()),
        store.layout().settingsBytes};
    const std::uint8_t settingsA[]{1u, 1u, 0u};
    const std::uint8_t settingsB[]{1u, 2u, 1u};
    std::uint8_t loadedSettings[8]{};
    std::size_t loadedSettingsSize{};
    assert(!settings.load(loadedSettings, sizeof loadedSettings, loadedSettingsSize));
    assert(settings.commit(settingsA, sizeof settingsA));
    assert(settings.load(loadedSettings, sizeof loadedSettings, loadedSettingsSize));
    assert(loadedSettingsSize == sizeof settingsA &&
           std::memcmp(loadedSettings, settingsA, sizeof settingsA) == 0);
    assert(settings.commit(settingsB, sizeof settingsB));
    assert(settings.load(loadedSettings, sizeof loadedSettings, loadedSettingsSize));
    assert(std::memcmp(loadedSettings, settingsB, sizeof settingsB) == 0);

    // Corrupt the newest settings slot; the previous committed generation is
    // still valid, just like the larger configuration store.
    memory.bytes[store.layout().settingsOffset() +
                 slstm32::storage::RedundantBlobStore::slotBytes + 16u] ^= 0x80u;
    assert(settings.load(loadedSettings, sizeof loadedSettings, loadedSettingsSize));
    assert(std::memcmp(loadedSettings, settingsA, sizeof settingsA) == 0);
}
