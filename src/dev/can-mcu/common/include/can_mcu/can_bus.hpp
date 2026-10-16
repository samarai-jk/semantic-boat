#pragma once

#include "slstm32/drivers/can_bus.hpp"
#include "slstm32/drivers/hardware.hpp"

namespace can_mcu {

// bxCAN (CAN1 on PA11/PA12) with the SN65HVD233 transceiver. The bit timing
// comes from CubeMX (250 kbit/s for NMEA 2000) and so do automatic bus-off
// recovery, automatic retransmission, and chronological transmit order.
// configure() accepts every frame into FIFO 0 and enables the receive,
// transmit-complete, and overrun interrupts; start() puts the transceiver in
// high-speed mode (PB1 low) and joins the bus; stop() leaves the bus and
// puts the transceiver in standby.
slstm32::drivers::CanBusHardware canBusHardware();

// Routes the HAL CAN interrupt callbacks to the driver. Call once before the
// driver's init().
void attachCanDriver(slstm32::drivers::CanBusDriver& driver);

// Yellow LED in the RJ45 connector (PB0, active low).
slstm32::drivers::DigitalOutputHardware canActivityLed();

} // namespace can_mcu
