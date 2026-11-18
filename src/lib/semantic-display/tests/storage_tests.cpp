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
        // Power cut after budget bytes: the write stops part way (at a byte;
        // the EEPROM's 64-byte pages make real tears coarser).
        if (budget >= 0) {
            const auto allowed = static_cast<std::size_t>(budget) < size ? static_cast<std::size_t>(budget) : size;
            std::memcpy(bytes.data() + address, input, allowed);
            budget -= static_cast<long>(allowed);
            if (allowed < size) { failWrites = true; return false; }
            return true;
        }
        std::memcpy(bytes.data() + address, input, size);
        return true;
    }
    std::array<std::uint8_t, 32768> bytes{};
    bool failWrites{};
    long budget{-1};
};

constexpr auto configA = R"json({"schema":"semantic-display/v1","sections":[{
  "id":"a","pages":[{"id":"p","grid":{"columns":1,"rows":1},
  "widgets":[{"type":"text","cell":{"column":0,"row":0},"text":"A"}]}]
}]})json";
constexpr auto configB = R"json({"schema":"semantic-display/v1","sections":[{
  "id":"b","pages":[{"id":"p","grid":{"columns":1,"rows":1},
  "widgets":[{"type":"text","cell":{"column":0,"row":0},"text":"B"}]}]
}]})json";

constexpr auto configC = R"json({"schema":"semantic-display/v1","sections":[{
  "id":"c","pages":[{"id":"p","grid":{"columns":1,"rows":1},
  "widgets":[{"type":"text","cell":{"column":0,"row":0},"text":"C"}]}]
}]})json";

struct Package {
    std::array<std::uint8_t, 1024> bytes{};
    std::size_t size{};
    std::uint32_t sourceCrc{};
};

Package compile(const char* json) {
    Package result;
    const auto compiled = semantic_display::ConfigCompiler{}.compile(json, result.bytes.data(), result.bytes.size());
    assert(compiled);
    result.size = compiled.packageSize;
    result.sourceCrc = semantic_display::PackageView{result.bytes.data(), result.size}.info().sourceCrc32;
    return result;
}

std::uint32_t activeCrc(MemoryStorage& memory) {
    semantic_display::ConfigCache cache{memory};
    std::array<std::uint8_t, 8192> output{};
    semantic_display::CachedConfig loaded{};
    if (!cache.loadActive(output.data(), output.size(), loaded)) return 0u;
    return semantic_display::PackageView{output.data(), loaded.packageSize}.info().sourceCrc32;
}

void cacheTests() {
    const auto a = compile(configA), b = compile(configB), c = compile(configC);
    std::array<std::uint8_t, 8192> scratch{};
    {
        MemoryStorage memory;
        semantic_display::ConfigCache cache{memory};
        static_assert(semantic_display::ConfigCacheLayout{}.slotBytes() == 6502u);
        static_assert(semantic_display::ConfigCacheLayout{}.maxPackageBytes() >= 6144u);
        static_assert(semantic_display::ConfigCacheLayout{}.settingsOffset() == 32512u);
        static_assert(semantic_display::ConfigCacheLayout{}.slotOffset(4u) + 6502u <= 32512u);
        semantic_display::CachedConfig stored{}, found{};
        assert(activeCrc(memory) == 0u);
        assert(cache.store("a.json", a.bytes.data(), a.size, stored) && stored.slot == 0u);
        assert(cache.store("b.json", b.bytes.data(), b.size, stored) && stored.slot == 1u);
        assert(activeCrc(memory) == b.sourceCrc);
        // Switching back to a cached app: one header write.
        assert(cache.find("a.json", found) && found.sourceCrc32 == a.sourceCrc);
        assert(cache.activate(found, scratch.data(), scratch.size()));
        assert(activeCrc(memory) == a.sourceCrc);
        // A new version of the active app goes into another slot; the old one goes.
        assert(cache.store("a.json", c.bytes.data(), c.size, stored) && stored.slot == 2u);
        assert(activeCrc(memory) == c.sourceCrc);
        std::array<semantic_display::CachedConfig, 8> entries{};
        std::size_t count{};
        assert(cache.list(entries.data(), entries.size(), count) && count == 2u);
        assert(entries[0].nameView() == "a.json" && entries[1].nameView() == "b.json");
        // Full: the least recently used app makes room, never the active one.
        assert(cache.store("d.json", a.bytes.data(), a.size, stored));
        assert(cache.store("e.json", b.bytes.data(), b.size, stored));
        assert(cache.store("f.json", c.bytes.data(), c.size, stored));
        assert(cache.list(entries.data(), entries.size(), count) && count == 5u);
        assert(cache.store("g.json", a.bytes.data(), a.size, stored));
        assert(!cache.find("b.json", found) && cache.find("a.json", found) && cache.find("f.json", found));
        // A corrupt package: the previous app stays active.
        memory.bytes[semantic_display::ConfigCacheLayout{}.slotOffset(stored.slot) + 128u + 10u] ^= 0x80u;
        semantic_display::CachedConfig loaded{};
        assert(cache.loadActive(scratch.data(), scratch.size(), loaded) && loaded.nameView() == "f.json");
        assert(cache.find("g.json", found) && !cache.activate(found, scratch.data(), scratch.size()));
    }
    // Power cut at every byte of a store and of an activation: always either
    // the previous or the new app, never none.
    for (int operation = 0; operation < 2; ++operation) {
        for (long budget = 0;; budget += 7) {
            MemoryStorage memory;
            semantic_display::ConfigCache cache{memory};
            semantic_display::CachedConfig stored{}, found{};
            assert(cache.store("a.json", a.bytes.data(), a.size, stored));
            assert(cache.store("b.json", b.bytes.data(), b.size, stored));
            memory.budget = budget;
            bool done{};
            if (operation == 0) done = cache.store("c.json", c.bytes.data(), c.size, stored);
            else done = cache.find("a.json", found) && cache.activate(found, scratch.data(), scratch.size());
            memory.budget = -1; memory.failWrites = false;
            const auto crc = activeCrc(memory);
            const auto wanted = operation == 0 ? c.sourceCrc : a.sourceCrc;
            assert(crc == b.sourceCrc || crc == wanted);
            if (done) { assert(crc == wanted); break; }
        }
    }
    // Taking over the app of the two-slot store (earlier firmware), without
    // touching its active copy; power cut at every byte of the take-over.
    for (long budget = 0;; budget += 7) {
        MemoryStorage memory;
        semantic_display::ConfigStore legacy{memory};
        semantic_display::StoredConfigInfo info{};
        assert(legacy.commit(a.bytes.data(), a.size, info));
        assert(legacy.commit(b.bytes.data(), b.size, info) && info.slot == 1u);
        memory.budget = budget;
        semantic_display::ConfigCache cache{memory};
        semantic_display::CachedConfig loaded{};
        std::array<std::uint8_t, 8192> output{};
        assert(cache.loadActive(output.data(), output.size(), loaded));
        assert((semantic_display::PackageView{output.data(), loaded.packageSize}.info().sourceCrc32 == b.sourceCrc));
        const bool done = !memory.failWrites;
        memory.budget = -1; memory.failWrites = false;
        // Earlier firmware still finds its app.
        semantic_display::StoredConfigInfo legacyInfo{};
        assert(legacy.load(output.data(), output.size(), legacyInfo) && legacyInfo.sourceCrc32 == b.sourceCrc);
        assert(activeCrc(memory) == b.sourceCrc);
        if (done) {
            assert(loaded.valid && loaded.slot == 0u && loaded.nameLength == 0u);
            break;
        }
    }
}

} // namespace

int main() {
    cacheTests();
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
