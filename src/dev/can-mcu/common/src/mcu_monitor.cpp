#include "can_mcu/mcu_monitor.hpp"
extern "C" {
#include "main.h"
extern ADC_HandleTypeDef hadc1;
}

namespace can_mcu {

bool McuMonitor::init() {
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) return false;
    resetWindow();
    if (!select(Source::supply)) return false;
    converting_ = HAL_ADC_Start(&hadc1) == HAL_OK;
    return converting_;
}

std::size_t McuMonitor::addInput(std::uint8_t adcChannel) {
    if (inputCount_ == maxInputs || adcChannel == 0u || adcChannel > 16u) return maxInputs;
    inputs_[inputCount_] = {this, adcChannel};
    return inputCount_++;
}

bool McuMonitor::sampleInput(void* context, std::int32_t& value) {
    const auto& input = *static_cast<const Input*>(context);
    if (!input.valid) return false;
    value = input.millivolts;
    return true;
}

slstm32::drivers::AnalogInputHardware McuMonitor::inputHardware(std::size_t index) {
    if (index >= maxInputs) return {};
    return {&inputs_[index], nullptr, &sampleInput};
}

bool McuMonitor::select(Source source, std::size_t input) {
    ADC_ChannelConfTypeDef channel{};
    switch (source) {
    case Source::supply: channel.Channel = ADC_CHANNEL_VREFINT; break;
    case Source::temperature: channel.Channel = ADC_CHANNEL_TEMPSENSOR; break;
    case Source::input: channel.Channel = __LL_ADC_DECIMAL_NB_TO_CHANNEL(inputs_[input].channel); break;
    }
    channel.Rank = ADC_REGULAR_RANK_1;
    // VREFINT needs at least 4 us and the temperature sensor 5 us of
    // sampling; 247.5 cycles at 32 MHz is 7.7 us, also plenty for external
    // inputs behind their 100 Ohm series resistors.
    channel.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
    channel.SingleDiff = ADC_SINGLE_ENDED;
    channel.OffsetNumber = ADC_OFFSET_NONE;
    source_ = source;
    measuringInput_ = input;
    return HAL_ADC_ConfigChannel(&hadc1, &channel) == HAL_OK;
}

void McuMonitor::run() {
    if (!converting_ || !__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_EOC)) return;
    const auto raw = static_cast<std::int32_t>(HAL_ADC_GetValue(&hadc1));

    if (source_ == Source::input) {
        auto& input = inputs_[measuringInput_];
        if (supplyMv_ != 0u) {
            input.sum += raw * static_cast<std::int32_t>(supplyMv_) / 4095;
            if (++input.count == inputAverage) {
                input.millivolts = input.sum / static_cast<std::int32_t>(inputAverage);
                input.valid = true;
                input.sum = 0;
                input.count = 0u;
            }
        }
        if (!select(Source::supply)) {
            converting_ = false;
            return;
        }
    } else if (source_ == Source::temperature) {
        if (supplyMv_ != 0u) {
            // Scale the reading to the 3.0 V calibration conditions, then
            // interpolate between the two factory points.
            const std::int32_t cal1 = *TEMPSENSOR_CAL1_ADDR;
            const std::int32_t cal2 = *TEMPSENSOR_CAL2_ADDR;
            const std::int32_t scaled = raw * supplyMv_ / static_cast<std::int32_t>(TEMPSENSOR_CAL_VREFANALOG);
            const std::int32_t milliCelsius =
                (scaled - cal1) * (TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP) * 1000 / (cal2 - cal1) +
                TEMPSENSOR_CAL1_TEMP * 1000;
            temperatureSum_ += milliCelsius;
            if (++temperatureCount_ == temperatureAverage) {
                dieMilliCelsius_ = temperatureSum_ / static_cast<std::int32_t>(temperatureAverage);
                temperatureValid_ = true;
                temperatureSum_ = 0;
                temperatureCount_ = 0u;
            }
        }
        if (!select(Source::supply)) {
            converting_ = false;
            return;
        }
    } else if (raw != 0) {
        const auto millivolts = static_cast<std::uint16_t>(
            __LL_ADC_CALC_VREFANALOG_VOLTAGE(static_cast<std::uint32_t>(raw), LL_ADC_RESOLUTION_12B));
        supplyMv_ = millivolts;
        if (millivolts < lowestMv_) lowestMv_ = millivolts;
        if (millivolts < window_.minimumMv) window_.minimumMv = millivolts;
        if (millivolts > window_.maximumMv) window_.maximumMv = millivolts;
        ++window_.samples;
        ++conversions_;
        bool selected = true;
        if (conversions_ % temperatureInterval == 0u) {
            selected = select(Source::temperature);
        } else if (inputCount_ != 0u && conversions_ % inputInterval == 0u) {
            selected = select(Source::input, nextInput_);
            nextInput_ = (nextInput_ + 1u) % inputCount_;
        }
        if (!selected) {
            converting_ = false;
            return;
        }
    }
    converting_ = HAL_ADC_Start(&hadc1) == HAL_OK;
}

McuMonitor::SupplyWindow McuMonitor::takeWindow() {
    const auto window = window_;
    resetWindow();
    return window;
}

void McuMonitor::resetWindow() {
    window_ = {0xffffu, 0u, 0u};
}

} // namespace can_mcu
