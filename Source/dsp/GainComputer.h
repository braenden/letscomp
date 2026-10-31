#pragma once

#include <algorithm>
#include <cmath>

namespace letscomp::dsp
{
class GainComputer
{
public:
    static float gainReductionDb(float inputLevelDb, float thresholdDb, float ratio,
                                 float kneeDb, float maximumReductionDb = 0.0f) noexcept
    {
        if (!std::isfinite(inputLevelDb) || !std::isfinite(thresholdDb))
            return 0.0f;

        ratio = std::isfinite(ratio) ? std::clamp(ratio, 1.0f, 1000.0f) : 1.0f;
        kneeDb = std::isfinite(kneeDb) ? std::clamp(kneeDb, 0.0f, 160.0f) : 0.0f;
        const auto slope = 1.0f - 1.0f / ratio;
        const auto over = inputLevelDb - thresholdDb;
        float reduction = 0.0f;

        if (kneeDb <= 1.0e-6f)
            reduction = over > 0.0f ? slope * over : 0.0f;
        else if (over <= -0.5f * kneeDb)
            reduction = 0.0f;
        else if (over >= 0.5f * kneeDb)
            reduction = slope * over;
        else
        {
            const auto kneePosition = over + 0.5f * kneeDb;
            reduction = slope * kneePosition * kneePosition / (2.0f * kneeDb);
        }

        if (maximumReductionDb > 0.0f)
            reduction = std::min(reduction, maximumReductionDb);
        return std::isfinite(reduction) ? std::max(0.0f, reduction) : 0.0f;
    }
};
}
