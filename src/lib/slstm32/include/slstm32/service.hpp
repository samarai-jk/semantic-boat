#pragma once

#include <array>
#include <cstddef>

namespace slstm32 {

class Service {
public:
    virtual ~Service() = default;
    virtual bool init() = 0;
    virtual void run() = 0;
    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }

private:
    bool enabled_{true};
};

template <std::size_t Capacity>
class ServiceManager {
public:
    bool add(Service& service) {
        if (size_ == Capacity) return false;
        services_[size_++] = &service;
        return true;
    }

    bool initAll() {
        bool ok = true;
        for (std::size_t i = 0; i < size_; ++i) ok = services_[i]->init() && ok;
        return ok;
    }

    void runAll() {
        for (std::size_t i = 0; i < size_; ++i) {
            if (services_[i]->enabled()) services_[i]->run();
        }
    }

private:
    std::array<Service*, Capacity> services_{};
    std::size_t size_{};
};

} // namespace slstm32
