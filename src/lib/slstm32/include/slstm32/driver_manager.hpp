#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "slstm32/driver.hpp"

namespace slstm32 {

template <std::size_t Capacity>
class DriverManager {
public:
    bool add(std::uint16_t id, Driver& driver) {
        if (size_ == Capacity || find(id) != nullptr) return false;
        entries_[size_++] = Entry{id, &driver};
        return true;
    }

    Driver* find(std::uint16_t id) const {
        for (std::size_t i = 0; i < size_; ++i) {
            if (entries_[i].id == id) return entries_[i].driver;
        }
        return nullptr;
    }

    template <typename T>
    T* findAs(std::uint16_t id) const { return static_cast<T*>(find(id)); }

    bool initAll() {
        bool ok = true;
        for (std::size_t i = 0; i < size_; ++i) ok = entries_[i].driver->init() && ok;
        return ok;
    }

    void runAll() {
        for (std::size_t i = 0; i < size_; ++i) {
            auto* driver = entries_[i].driver;
            if (driver->enabled()) driver->run();
        }
    }

    std::size_t size() const { return size_; }

private:
    struct Entry { std::uint16_t id{}; Driver* driver{}; };
    std::array<Entry, Capacity> entries_{};
    std::size_t size_{};
};

} // namespace slstm32
