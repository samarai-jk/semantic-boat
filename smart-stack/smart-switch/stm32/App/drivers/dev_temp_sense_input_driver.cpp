#include "drivers/dev_temp_sense_input_driver.hpp"
#include <array>
#include <cmath>

namespace app
{

    float DevTempSenseInputDriver::temperatureC() const
    {
        // Interpolate using the provided NTC 10k resistance table.
        // Steps:
        // 1) Convert measured voltage (valueV) to NTC resistance using divider math.
        //    Divider assumed: Vs --- Rfixed --- node --- NTC --- GND (node measured).
        // 2) Interpolate temperature from the (R, T) table (R decreases with T).

        // Supply/reference used by valueV()
        constexpr float Vs = 1.8f;

        // At 25°C, R0=10k and measured V25=0.9115 V -> infer fixed resistor.
        constexpr float R0 = 10000.0f;   // ohms
        constexpr float V25 = 0.9115f;   // volts (exact per user)
        constexpr float Rfixed = R0 * (Vs - V25) / V25; // ~9747.6 ohms

        // NTC 10k table: resistance (ohms) vs temperature (°C), descending R (low T first).
        struct LutEntry { float R_ohm; float tC; };
        static constexpr std::array<LutEntry, 21> kResToTempLUT = { {
            { 335670.0f, -40.0f },
            { 176680.0f, -30.0f },
            {  96790.0f, -20.0f },
            {  55350.0f, -10.0f },
            {  32650.0f,   0.0f },
            {  19900.0f,  10.0f },
            {  12490.0f,  20.0f },
            {  10000.0f,  25.0f },
            {   8060.0f,  30.0f },
            {   5320.0f,  40.0f },
            {   3600.0f,  50.0f },
            {   2490.0f,  60.0f },
            {   1750.0f,  70.0f },
            {   1260.0f,  80.0f },
            {    920.0f,  90.0f },
            {    680.0f, 100.0f },
            {    510.0f, 110.0f },
            {    390.0f, 120.0f },
            {    300.0f, 130.0f },
            {    230.0f, 140.0f },
            {    180.0f, 150.0f },
        } };

        // Read current voltage
        const float v = valueV();
        if (std::isnan(v))
        {
            return std::nanf("");
        }
        if (v <= 0.0f || v >= Vs)
        {
            return std::nanf("");
        }

        // Convert voltage to resistance: V = Vs * Rntc / (Rfixed + Rntc)
        const float Rntc = (Rfixed * v) / (Vs - v);
        if (!(Rntc > 0.0f))
        {
            return std::nanf("");
        }

        // Clamp to table range
        if (Rntc >= kResToTempLUT.front().R_ohm)
        {
            return kResToTempLUT.front().tC;
        }
        if (Rntc <= kResToTempLUT.back().R_ohm)
        {
            return kResToTempLUT.back().tC;
        }

        // Find bounding segment [i, i+1] such that R_i >= Rntc >= R_{i+1}
        for (size_t i = 0; i + 1 < kResToTempLUT.size(); ++i)
        {
            const auto &a = kResToTempLUT[i];
            const auto &b = kResToTempLUT[i + 1];
            if (Rntc <= a.R_ohm && Rntc >= b.R_ohm)
            {
                const float dR = a.R_ohm - b.R_ohm; // positive
                if (dR <= 0.0f)
                {
                    // Guard against malformed table
                    return a.tC;
                }
                const float alpha = (a.R_ohm - Rntc) / dR; // 0..1
                return a.tC + alpha * (b.tC - a.tC);
            }
        }

        // Should not reach here
        return std::nanf("");
    }

}
