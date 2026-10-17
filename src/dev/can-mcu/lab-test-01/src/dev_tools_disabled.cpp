// Stand-in for src/dev/dev_tools.cpp in builds without development tools
// (see dev_tools.hpp and AGENTS.md).
#include "dev_tools.hpp"

namespace lab_test_01 {
namespace {

class NoDevTools final : public slstm32::Service {
public:
    bool init() override { return true; }
    void run() override {}
};

} // namespace

void devToolsInterrupt(std::uint16_t) {}

slstm32::Service& devTools(const DevToolsContext&) {
    static NoDevTools none;
    return none;
}

} // namespace lab_test_01
