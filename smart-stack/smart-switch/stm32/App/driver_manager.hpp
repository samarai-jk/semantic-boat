#ifndef APP_DRIVER_MANAGER_HPP
#define APP_DRIVER_MANAGER_HPP

#include <cstddef>
#include <cstdint>
#include <array>
#include <utility>
#include "driver.hpp"

namespace app {

// Keep it tiny and static: fixed capacity registry
template <std::size_t Capacity>
class DriverManagerT {
public:

    static DriverManagerT& instance() {
        static DriverManagerT g;
        return g;
    }

    // Register a driver pointer with an ID; returns false if full
    bool registerDriver(uint16_t id, IDriver* drv) {
        if (!drv) return false;
        // prevent duplicates by id
        for (std::size_t i = 0; i < count_; ++i) {
            if (entries_[i].id == id) return false;
        }
        if (count_ >= Capacity) return false;
        entries_[count_++] = {id, drv};
        return true;
    }

    // Find driver by id; nullptr if not found
    IDriver* get(uint16_t id) const {
        for (std::size_t i = 0; i < count_; ++i) {
            if (entries_[i].id == id) return entries_[i].drv;
        }
        return nullptr;
    }

    // Initialize all registered drivers
    void initAll() {
        for (std::size_t i = 0; i < count_; ++i) {
            entries_[i].drv->init();
        }
    }

    // Run all drivers once
    void runAll() {
        for (std::size_t i = 0; i < count_; ++i) {
            entries_[i].drv->run();
        }
    }

    std::size_t count() const { return count_; }

private:
    DriverManagerT() = default;

    struct Entry { uint16_t id; IDriver* drv; };
    std::array<Entry, Capacity> entries_{};
    std::size_t count_ = 0;
};

// Application-wide manager type with modest capacity
using DriverManager = DriverManagerT<8>;

} // namespace app

#endif // APP_DRIVER_MANAGER_HPP