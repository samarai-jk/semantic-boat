#include "drivers/dev_temp_sense_input_driver.hpp"
#include <cmath>

namespace app
{

    float DevTempSenseInputDriver::temperatureC() const
    {
        // Convert measured voltage to temperature (°C) for a 10k NTC using the Beta model.
        // Calibration: at 25°C, valueV() ≈ 0.870 V.
        // Assumption: Divider is Vs --- Rfixed --- node --- NTC --- GND (NTC to GND).
        // This lets us compute Rfixed from the 25°C calibration point without knowing it a priori.

        const float v = valueV();
        if (std::isnan(v))
        {
            return std::nanf("");
        }

        constexpr float Vs = 1.8f; // SDADC reference used in valueV()
        if (v <= 0.0f || v >= Vs)
        {
            return std::nanf("");
        }

        // Thermistor parameters
        constexpr float R0 = 10000.0f;        // 10k at 25°C
        constexpr float T0 = 25.0f + 273.15f; // Kelvin
        constexpr float Beta = 3435.0f;       // Typical 10k NTC B25/85 (adjust if needed)

        // Calibration point
        constexpr float V25 = 0.9113f; // Measured divider voltage at 25°C

        // Compute fixed resistor from the 25°C point for the assumed divider orientation
        // Rfixed = R0 * (Vs - V25) / V25
        const float Rfixed = R0 * (Vs - V25) / V25;

        // Infer current NTC resistance from current voltage
        // Rntc = Rfixed * v / (Vs - v)
        const float R = Rfixed * v / (Vs - v);
        if (R <= 0.0f)
        {
            return std::nanf("");
        }

        // Beta equation: 1/T = 1/T0 + (1/B) * ln(R/R0)
        const float invT = (1.0f / T0) + (1.0f / Beta) * std::log(R / R0);
        if (invT <= 0.0f)
        {
            return std::nanf("");
        }
        const float T = 1.0f / invT;
        return T - 273.15f;
    }

}
