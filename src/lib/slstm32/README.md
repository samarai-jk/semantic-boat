# slstm32

`slstm32` is a small C++17 application framework and reusable driver library for
bare-metal STM32 firmware. It provides the structure that sits between generated
hardware initialization code and product-specific application logic.

The library is designed for firmware that uses the STM32 HAL or LL directly,
without requiring an RTOS, PlatformIO, dynamic allocation, exceptions, or RTTI.
The reusable code does not include STM32 HAL headers. A firmware project connects
it to a particular MCU, pinout, and CubeMX configuration through callback-based
hardware bindings.

This document is both an introduction and a maintenance guide. Read it before
adding a driver, service, or new firmware application.

## Design goals

- Keep reusable behavior independent of STM32 family and board pinout.
- Keep ownership and memory usage explicit and deterministic.
- Separate hardware mechanics from application policy.
- Make interrupt handlers short and defer application work to the main loop.
- Prefer non-blocking state machines driven by `run()` over delays and polling
  loops.
- Register each driver and service once, in one composition root.
- Make dependencies visible in constructors instead of hiding them in global
  singletons or numeric-ID lookups.

`slstm32` is not an RTOS, dependency-injection container, HAL replacement, or
device-tree system. It is intentionally small enough to understand from its
headers.

## Architecture at a glance

The intended dependency direction is:

```text
application services
        |
        v
generic slstm32 drivers and event/executor APIs
        |
        v
firmware-owned hardware bindings
        |
        v
STM32 HAL/LL and CubeMX-generated handles
```

The layers have distinct responsibilities:

- A **hardware binding** translates generic operations such as `write(bool)` or
  `sample(int32_t&)` into HAL/LL calls for one board.
- A **driver** owns reusable hardware behavior: sampling, debouncing, PWM level,
  LED timing, relay pulse timing, or display protocol.
- A **service** implements application behavior by coordinating drivers and
  reacting to events. Services should not know pin numbers or HAL handles.
- The **application composition root** constructs everything, supplies
  dependencies, and lists every driver and service once.
- `Application` initializes and runs the registered components.

## Directory structure

```text
slstm32/
|-- CMakeLists.txt
|-- README.md
|-- include/slstm32/
|   |-- application.hpp       component catalog and lifecycle
|   |-- driver.hpp            Driver base class
|   |-- service.hpp           Service base class and legacy manager
|   |-- runtime.hpp           time and critical-section hooks
|   |-- event_bus.hpp         deferred bounded event queue
|   |-- executor.hpp          cooperative task and timer executor
|   |-- error.hpp             minimal error reporting abstraction
|   |-- log.hpp               application-provided logging sink
|   |-- drivers/              generic hardware drivers and bindings
|   |-- storage/              byte-addressable persistent-storage interface
|   `-- epd/                  canvas, region queue, and E-paper support
`-- src/                      non-template implementations
```

Public headers live below `include/slstm32`. Implementations that require a
translation unit live below `src` and must be added to `CMakeLists.txt`.

## Build integration

Add the library to the firmware's CMake build and link its target:

```cmake
add_subdirectory(path/to/slstm32 ${CMAKE_CURRENT_BINARY_DIR}/slstm32)

add_executable(firmware
    src/app.cpp
    src/board_hardware.cpp
    src/example_service.cpp
)

target_link_libraries(firmware PRIVATE slstm32 generated_stm32_target)
```

The `slstm32` target exports its include directory and requires C++17. Its own
sources are compiled with warnings enabled and with exceptions, RTTI, and
thread-safe local-static initialization disabled.

The library can also be configured by itself for host compilation:

```sh
cmake -S src/lib/slstm32 -B build/slstm32-host
cmake --build build/slstm32-host
```

A host build verifies portable library code, but it does not replace a clean
cross-compiled firmware build or hardware test.

## Runtime hooks

Several facilities need time or critical sections but must not depend directly
on the STM32 HAL. The firmware supplies these operations through `Runtime`:

```cpp
#include "slstm32/runtime.hpp"

std::uint32_t milliseconds() {
    return HAL_GetTick();
}

void enterCritical() {
    // Disable interrupts while preserving the previous interrupt state.
}

void exitCritical() {
    // Restore the interrupt state saved by enterCritical().
}

const slstm32::Runtime runtime{
    &milliseconds,
    &enterCritical,
    &exitCritical,
};
```

The critical-section hooks must be safe when called from normal code and from an
ISR if `publishFromIsr()` or `postFromIsr()` is used. They should preserve prior
interrupt state and support the nesting behavior required by the application.
Do not blindly enable interrupts in `exitCritical()`.

Milliseconds are represented as wrapping `std::uint32_t` values. Timing code
should compare deadlines with signed subtraction, as the existing drivers do,
so normal counter wraparound remains safe.

## Drivers

A driver derives from `slstm32::Driver`:

```cpp
class ExampleDriver final : public slstm32::Driver {
public:
    bool init() override;
    void run() override;
};
```

`init()` configures the reusable component and returns whether initialization
succeeded. `run()` advances non-blocking work and should return quickly. Disabled
drivers are initialized normally but their `run()` methods are skipped.

Drivers should contain hardware mechanism, not product policy. For example, an
LED driver may implement `pulse(50)`, while deciding that an error deserves
three pulses belongs in a service.

### Hardware bindings

The callback structures in `drivers/hardware.hpp` are the boundary between the
portable library and board-specific code:

- `DigitalInputHardware`
- `DigitalOutputHardware`
- `PwmOutputHardware`
- `ToneOutputHardware`
- `AnalogInputHardware`

Each binding contains a `void* context` and plain function pointers. The context
usually points to a static structure holding a HAL handle, GPIO port, pin, timer
channel, or ADC channel. Capturing lambdas cannot be used as these callbacks.

Example digital-output binding:

```cpp
#include "slstm32/drivers/hardware.hpp"

namespace board {

struct GpioOutput {
    GPIO_TypeDef* port;
    std::uint16_t pin;
};

bool configureOutput(void* opaque) {
    const auto& output = *static_cast<GpioOutput*>(opaque);
    GPIO_InitTypeDef config{};
    config.Pin = output.pin;
    config.Mode = GPIO_MODE_OUTPUT_PP;
    config.Pull = GPIO_NOPULL;
    config.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(output.port, &config);
    return true;
}

void writeOutput(void* opaque, bool physicalLevel) {
    const auto& output = *static_cast<GpioOutput*>(opaque);
    HAL_GPIO_WritePin(output.port, output.pin,
                      physicalLevel ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

GpioOutput statusPin{GPIOA, GPIO_PIN_0};

slstm32::drivers::DigitalOutputHardware statusOutput() {
    return {&statusPin, &configureOutput, &writeOutput, true};
}

} // namespace board
```

`activeHigh` converts logical driver state to physical pin state. An active-low
output receives `false` from its write callback when the logical output is on.
An active-low input reports logical `true` when its read callback returns a low
physical level.

Callback contexts and configuration arrays are not copied by every driver.
They must remain alive as long as the driver that references them. Static
storage is normally simplest in embedded firmware.

### Available generic drivers

| Driver | Purpose and important behavior |
| --- | --- |
| `DigitalInputDriver` | Samples one logical digital input on every `run()`. |
| `DigitalOutputDriver` | Controls one logical output and remembers its requested state. |
| `ButtonDriver` | Maps up to 16 interrupt tokens to button IDs, applies per-button debounce, and queues `ButtonEvent` values. Call `onInterrupt()` from the GPIO callback. |
| `LedDriver` | Digital LED with steady, pulse, and blink modes. Timing is non-blocking. |
| `PwmOutputDriver` | Controls one normalized PWM duty value in the range 0.0 to 1.0. |
| `RgbLedDriver` | Controls red, green, and blue PWM bindings; supports common-anode inversion and non-blocking blinking. |
| `ToneDriver` | Starts a hardware tone at a normalized output level and stops it after a non-blocking duration. Call `onTick()` from a periodic interrupt when tone deadlines must remain accurate during long foreground work; the board's stop callback must then be interrupt-safe. |
| `AnalogInputDriver` | Samples an integer source, maintains a moving average of up to 128 samples, and applies `(average - offset) * scale`. |
| `LatchingRelayDriver` | Drives set/reset coils with non-blocking pulses and queues a changed request while a pulse is active. |
| `At24c256` | Implements the generic `ByteStorage` interface for a 32 KiB, 64-byte-page AT24C256-compatible EEPROM using board-provided read/write-page callbacks. |

`storage::RedundantBlobStore` persists blobs of up to 40 bytes in two
alternating 64-byte slots. CRC validation and a commit marker written last keep
the previous generation usable if power is lost during a settings write. It is
intended for small device preferences, calibration values, and similar setup
data rather than large application packages.

All PWM and RGB values are normalized floats. Board code is responsible for
mapping them to the timer's actual auto-reload range.

### Button interrupt modes

`ButtonConfig::interrupt` describes what a hardware interrupt means:

- `ButtonInterrupt::pressed`: the configured edge itself means pressed.
- `ButtonInterrupt::released`: the configured edge itself means released.
- `ButtonInterrupt::sampleInput`: read the input immediately and derive state
  using `activeHigh`.

Use `pressed` or `released` for a single-edge EXTI configuration. This captures
the edge immediately and avoids losing it by sampling the pin after bounce.
`ButtonDriver::onInterrupt()` only performs mapping, debounce bookkeeping, and
event enqueueing; event handlers run later when `EventBus::process()` is called.
Call `resetDebounce()` when entering a mode that freezes the runtime clock, such
as MCU sleep, so the first wake edge is not compared with a stale pre-sleep
timestamp.

## Services

A service derives from `slstm32::Service` and receives its dependencies through
its constructor:

```cpp
class HealthService final : public slstm32::Service {
public:
    explicit HealthService(slstm32::drivers::LedDriver& led) : led_(led) {}

    bool init() override {
        led_.blink(1000u, 100u);
        return true;
    }

    void run() override {
        // Advance application policy here. Do not block.
    }

private:
    slstm32::drivers::LedDriver& led_;
};
```

Services may subscribe to events, maintain application state, and call driver
methods. They should not configure GPIO, call board-specific HAL peripherals, or
hide dependencies behind global getters.

Disabled services remain initialized but their `run()` calls are skipped.

## Application composition

`application.hpp` replaces the older pattern of numeric driver IDs, factory
switches, manager registration calls, and hand-written store getters.

Create all components in dependency order, define empty tag types, and bind each
component once:

```cpp
#include "slstm32/application.hpp"
#include "slstm32/drivers/led.hpp"

namespace component {
struct StatusLed;
struct Health;
} // namespace component

slstm32::drivers::LedDriver statusLed{runtime, board::statusOutput()};
HealthService health{statusLed};

auto application = slstm32::makeApplication(
    slstm32::makeDriverSet(
        slstm32::bind<component::StatusLed>(statusLed)),
    slstm32::makeServiceSet(
        slstm32::bind<component::Health>(health)));
```

Tags provide compile-time lookup and permit several instances of the same driver
type:

```cpp
auto& led = application.drivers().get<component::StatusLed>();
```

Important lifecycle rules:

- `Application` stores pointers; it does not own or copy drivers and services.
- Every bound component and every object referenced by it must outlive the
  application.
- `application.init()` initializes all drivers in declaration order. It returns
  `false` and does not initialize services if any driver failed.
- Driver initialization attempts continue through the complete driver set and
  produce one aggregate success/failure result.
- Services initialize in declaration order after all drivers succeed.
- `application.run()` runs enabled drivers first, then enabled services, in
  declaration order.
- A tag must occur exactly once within its component set when used with `get()`.

The composition root should be the only place that knows every concrete driver
and service. Elsewhere, pass narrow dependencies by reference.

### Main-loop skeleton

The event bus and executor are independent facilities and are not automatically
owned or processed by `Application`:

```cpp
slstm32::EventBus events{runtime};
slstm32::BareExecutor executor{runtime};

void firmwareInit() {
    if (!application.init()) {
        // Record the failure and enter the project's safe state.
    }
}

[[noreturn]] void firmwareLoop() {
    for (;;) {
        events.process();
        executor.process();
        application.run();
    }
}
```

Pick and document one loop order for a firmware. Keep handlers and `run()` calls
short so every component continues to make progress.

## Event bus

`EventBus` provides deferred, in-order event delivery:

```cpp
inline constexpr slstm32::EventId measurementReady = 1u;

struct MeasurementEvent {
    std::int16_t value;
};

void onMeasurement(slstm32::EventId, const void* payload,
                   std::uint8_t size, void* context) {
    if (!context || !payload || size != sizeof(MeasurementEvent)) return;
    auto& service = *static_cast<HealthService*>(context);
    const auto& event = *static_cast<const MeasurementEvent*>(payload);
    // Forward the value to a typed service method.
}

events.subscribe(measurementReady, &onMeasurement, &health);
events.publish(measurementReady, MeasurementEvent{42});
```

Properties and limits:

- Payloads are copied into the queue and may contain at most 16 bytes.
- Use small, trivially copyable payload structures. Never enqueue pointers to
  short-lived data as a substitute for payload ownership.
- There are 24 subscription slots.
- The 32-entry ring buffer can hold 31 pending events.
- `publish()` and `publishFromIsr()` return `false` for invalid or full queues.
- `droppedEvents()` reports full-queue drops.
- Duplicate identical subscriptions are treated as success without consuming a
  second slot.
- Handlers execute synchronously inside `process()`, not in the publishing ISR.

Always check subscription failures during initialization. Decide explicitly
whether a publish failure may be dropped, retried, or treated as a fault.

## Cooperative executor

`BareExecutor` queues plain function/context tasks and provides one-shot and
periodic timers:

```cpp
void sampleTask(void* context) {
    static_cast<SensorService*>(context)->sampleNow();
}

const auto timer = executor.callEvery(100u, {&sampleTask, &sensorService});
if (timer == slstm32::invalidTimer) {
    // No timer slot was available.
}
```

- The task ring has 32 entries and can hold 31 pending tasks.
- There are 16 timer slots.
- `postFromIsr()` defers execution until `process()`.
- Timer callbacks are posted as tasks; they do not execute in the timer scan.
- Periodic timers advance from their previous deadline and skip missed periods
  rather than running a burst of catch-up calls.
- Tasks execute synchronously and must not block.

As with the event bus, ISR use requires valid critical-section hooks.

## Logging and errors

`Logger` forwards a level and message to an application-owned sink. It performs
no formatting and owns neither the sink context nor message storage.

`ErrorReporter` remembers the last `Error` and writes its message through a
`Logger`. It is deliberately minimal. A firmware may wrap it with persistent
fault storage, telemetry, or a safe-state policy without coupling those choices
to the shared library.

## E-paper support

The `epd` namespace provides HAL-independent panel interfaces, an asynchronous
update lifecycle, a monochrome canvas, panel drivers, and the generic
`RenderQueue<N>` dirty-region queue.

See [EPD.md](EPD.md) for the complete driver, rendering-queue, cancellation,
partial-refresh, coordinate-mapping, and integration documentation.

## Adding a new generic driver

1. Confirm that the behavior is reusable across more than one board. A class
   that only names one board's pins belongs in that firmware project.
2. Define the smallest HAL-independent hardware binding needed by the behavior.
3. Derive from `Driver`; make `init()` report configuration failures and make
   `run()` non-blocking.
4. Store no references to temporary bindings, arrays, or contexts.
5. Keep policy in services. A driver should expose capability and state.
6. Put the public header under `include/slstm32/drivers` and implementation under
   `src/drivers`.
7. Add the implementation to `slstm32/CMakeLists.txt`.
8. Test timing boundaries, counter wraparound, queue-full behavior, active-low
   logic, repeated commands, and failure callbacks as applicable.
9. Run both a host build and at least one clean ARM target build.
10. Update this README's driver table and any affected constraints.

## Adding a service

1. Derive from `Service` in the firmware project unless the entire policy is
   genuinely reusable.
2. Inject drivers, event buses, executors, clocks, and peer services explicitly
   through the constructor.
3. Subscribe to events in `init()` and fail initialization if required
   subscriptions cannot be installed.
4. Keep event handlers short; record work and complete it incrementally in
   `run()` when appropriate.
5. Add the service to the single `makeServiceSet(...)` declaration.

## Legacy managers

`DriverManager` and `ServiceManager` remain available for older code. New
firmware should prefer `makeApplication`, tagged bindings, and constructor
injection. Do not introduce a new numeric-ID enum, factory switch, or global
driver-store getter layer.

## Constraints and common mistakes

- Component lifetime is external. The application catalog contains pointers.
- Hardware callback contexts must outlive their drivers.
- Configuration arrays passed by pointer, such as button definitions, must also
  outlive their drivers.
- `run()` is cooperative. A blocking driver or service stalls every component
  after it.
- Disabling a component only skips `run()`. Direct method calls may still change
  hardware state.
- Do not use `HAL_Delay()` inside generic driver state machines.
- Do not include STM32-family HAL headers from reusable `slstm32` headers or
  sources.
- Do not perform complex application work from an ISR. Capture the occurrence
  and defer it through an event, task, or small state flag.
- Event IDs are application-owned. Keep their declarations centralized and
  avoid accidental reuse.
- Check every bounded operation that can fail: subscription, publish, post,
  timer creation, binding configuration, and driver initialization.
- Host compilation proves portability and syntax, not electrical behavior,
  timing, interrupt configuration, or successful flashing.

## Maintainer checklist for humans and AI agents

Before changing the library:

1. Read the relevant public header and its complete implementation.
2. Inspect at least one caller to understand ownership and timing assumptions.
3. Decide whether the change is a hardware mechanism, generic driver behavior,
   or application policy, and place it in the matching layer.
4. Preserve fixed-capacity and no-allocation behavior unless the project makes a
   deliberate architectural change.
5. Preserve interrupt safety and unsigned-millisecond wraparound behavior.
6. Avoid parallel abstractions that duplicate `Runtime`, `EventBus`, hardware
   bindings, or the application catalog.
7. Update CMake and this document when public structure changes.
8. Validate with compiler warnings enabled, a host test where practical, and a
   clean target build. State clearly when hardware behavior remains untested.

The preferred result is boring, explicit firmware: board code translates HAL
operations, drivers implement reusable mechanics, services express policy, and
one composition root shows how the complete application fits together.
