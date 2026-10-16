#include "can_mcu/can_bus.hpp"
#include <cstring>
extern "C" {
#include "main.h"
extern CAN_HandleTypeDef hcan1;
}

namespace can_mcu {
namespace {

constexpr std::uint32_t canIrqPriority = 6u;
constexpr std::uint16_t mutePin = GPIO_PIN_1;     // PB1 -> transceiver Rs
constexpr std::uint16_t activityPin = GPIO_PIN_0; // PB0 -> yellow LED cathode

slstm32::drivers::CanBusDriver* attachedDriver{};

void setTransceiverStandby(bool standby) {
    HAL_GPIO_WritePin(GPIOB, mutePin, standby ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool configure(void*) {
    CAN_FilterTypeDef filter{};
    filter.FilterBank = 0u;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0u;
    filter.FilterIdLow = 0u;
    filter.FilterMaskIdHigh = 0u;
    filter.FilterMaskIdLow = 0u;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14u;
    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK) return false;

    for (const auto irq : {CAN1_TX_IRQn, CAN1_RX0_IRQn, CAN1_SCE_IRQn}) {
        HAL_NVIC_SetPriority(irq, canIrqPriority, 0u);
    }
    // Error frames are not interrupt-driven: without an acknowledging node
    // they repeat for every retransmission. Bus state is polled instead.
    return HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING |
                                                    CAN_IT_RX_FIFO0_OVERRUN |
                                                    CAN_IT_TX_MAILBOX_EMPTY) == HAL_OK;
}

bool start(void*) {
    setTransceiverStandby(false);
    return HAL_CAN_Start(&hcan1) == HAL_OK;
}

void stop(void*) {
    HAL_CAN_Stop(&hcan1);
    setTransceiverStandby(true);
}

bool transmit(void*, const slstm32::drivers::CanFrame& frame) {
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0u) return false;
    CAN_TxHeaderTypeDef header{};
    header.IDE = frame.extended ? CAN_ID_EXT : CAN_ID_STD;
    header.ExtId = frame.extended ? frame.id : 0u;
    header.StdId = frame.extended ? 0u : frame.id;
    header.RTR = frame.remote ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    header.DLC = frame.length > 8u ? 8u : frame.length;
    header.TransmitGlobalTime = DISABLE;
    std::uint32_t mailbox{};
    return HAL_CAN_AddTxMessage(&hcan1, &header, const_cast<std::uint8_t*>(frame.data.data()),
                                &mailbox) == HAL_OK;
}

slstm32::drivers::CanStatus status(void*) {
    using slstm32::drivers::CanState;
    const auto esr = hcan1.Instance->ESR;
    slstm32::drivers::CanStatus result{};
    result.transmitErrors = static_cast<std::uint8_t>((esr & CAN_ESR_TEC) >> CAN_ESR_TEC_Pos);
    result.receiveErrors = static_cast<std::uint8_t>((esr & CAN_ESR_REC) >> CAN_ESR_REC_Pos);
    result.lastErrorCode = static_cast<std::uint8_t>((esr & CAN_ESR_LEC) >> CAN_ESR_LEC_Pos);
    if (HAL_CAN_GetState(&hcan1) != HAL_CAN_STATE_LISTENING) {
        result.state = CanState::stopped;
    } else if (esr & CAN_ESR_BOFF) {
        result.state = CanState::busOff;
    } else if (esr & CAN_ESR_EPVF) {
        result.state = CanState::errorPassive;
    } else if (esr & CAN_ESR_EWGF) {
        result.state = CanState::errorWarning;
    } else {
        result.state = CanState::errorActive;
    }
    return result;
}

bool configureActivityLed(void*) {
    return true; // PB0 is a push-pull output from CubeMX, initially high (off).
}

void writeActivityLed(void*, bool high) {
    HAL_GPIO_WritePin(GPIOB, activityPin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void transmitComplete() {
    if (attachedDriver) attachedDriver->onTransmitComplete();
}

} // namespace

slstm32::drivers::CanBusHardware canBusHardware() {
    return {nullptr, &configure, &start, &stop, &transmit, &status};
}

void attachCanDriver(slstm32::drivers::CanBusDriver& driver) {
    attachedDriver = &driver;
}

slstm32::drivers::DigitalOutputHardware canActivityLed() {
    return {nullptr, &configureActivityLed, &writeActivityLed, false};
}

} // namespace can_mcu

extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* can) {
    CAN_RxHeaderTypeDef header{};
    slstm32::drivers::CanFrame frame{};
    while (HAL_CAN_GetRxFifoFillLevel(can, CAN_RX_FIFO0) != 0u) {
        if (HAL_CAN_GetRxMessage(can, CAN_RX_FIFO0, &header, frame.data.data()) != HAL_OK) return;
        frame.extended = header.IDE == CAN_ID_EXT;
        frame.id = frame.extended ? header.ExtId : header.StdId;
        frame.remote = header.RTR == CAN_RTR_REMOTE;
        frame.length = static_cast<std::uint8_t>(header.DLC > 8u ? 8u : header.DLC);
        if (can_mcu::attachedDriver) can_mcu::attachedDriver->onFrameReceived(frame);
    }
}

extern "C" void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef*) { can_mcu::transmitComplete(); }
extern "C" void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef*) { can_mcu::transmitComplete(); }
extern "C" void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef*) { can_mcu::transmitComplete(); }

extern "C" void HAL_CAN_ErrorCallback(CAN_HandleTypeDef* can) {
    if ((can->ErrorCode & HAL_CAN_ERROR_RX_FOV0) != 0u && can_mcu::attachedDriver) {
        can_mcu::attachedDriver->onReceiveOverrun();
    }
    HAL_CAN_ResetError(can);
}
