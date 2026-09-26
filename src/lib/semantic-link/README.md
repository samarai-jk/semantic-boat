# Semantic Link

Semantic Link is the small transport-independent protocol used between a
Semantic Boat display and a data gateway or development simulator. It contains
no UART, RS485, CAN, Signal K, or operating-system code. The firmware and host
tools share the wire-format rules while supplying their own transports.

See [PROTOCOL.md](PROTOCOL.md) for the wire format and message definitions.
