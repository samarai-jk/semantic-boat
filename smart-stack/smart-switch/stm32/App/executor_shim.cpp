#include "executor.hpp"

extern "C" void app_executor_pendsv_service(void) {
    app::Executor::instance().process();
}
