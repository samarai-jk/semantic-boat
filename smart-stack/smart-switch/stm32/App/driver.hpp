#ifndef APP_DRIVER_HPP
#define APP_DRIVER_HPP

#include <cstdint>

namespace app
{

    class IDriver
    {
    public:
        virtual ~IDriver() = default;
        virtual void init() = 0; // called once during app startup
        virtual void run() = 0;  // called periodically from app loop
    };

} // namespace app

#endif // APP_DRIVER_HPP