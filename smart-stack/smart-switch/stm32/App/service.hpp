#ifndef APP_SERVICE_HPP
#define APP_SERVICE_HPP

#include <cstdint>
#include <cstddef>

namespace app {

class IService {
public:
    virtual ~IService() = default;
    virtual void init() = 0;
    virtual void run() = 0;
    virtual void setEnabled(bool en) { enabled_ = en; }
    virtual bool isEnabled() const { return enabled_; }
protected:
    bool enabled_{true};
};

template <size_t Capacity>
class ServiceManagerT {
public:
    bool registerService(IService* s) {
        if (count_ >= Capacity) return false;
        services_[count_++] = s;
        return true;
    }
    void initAll() {
        for (size_t i=0;i<count_;++i) if (services_[i]) services_[i]->init();
    }
    void runAll() {
        for (size_t i=0;i<count_;++i) if (services_[i] && services_[i]->isEnabled()) services_[i]->run();
    }
    void setAllEnabled(bool en) {
        for (size_t i=0;i<count_;++i) if (services_[i]) services_[i]->setEnabled(en);
    }
private:
    IService* services_[Capacity]{};
    size_t count_{0};
};

using ServiceManager = ServiceManagerT<8>;

inline ServiceManager& ServiceManagerInstance() {
    static ServiceManager s; return s;
}

} // namespace app

#endif // APP_SERVICE_HPP
