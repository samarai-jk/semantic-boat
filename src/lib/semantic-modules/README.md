# semantic-modules

Drivers for Semantic Boat peripheral modules: small boards that plug into a
host (for example host-dev) through I2C connectors and pin headers. The
drivers are HAL-free and built on slstm32: generic chips (ADCs, sensors) use
slstm32 drivers, and this library adds what is specific to a module, such as
divider ratios, input polarity, and channel mapping. Board code supplies the
I2C bus and pin bindings.

| Module | Driver | Hardware notes |
| --- | --- | --- |
| 12v-ad-input-iso | `AdInputIso12v` | [12v-ad-input-iso.md](12v-ad-input-iso.md) |
