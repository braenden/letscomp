#pragma once

#include <algorithm>
#include <cmath>

namespace letscomp::dsp
{
class EnvelopeFollower
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::isfinite(newSampleRate) ? std::clamp(newSampleRate, 1.0, 768000.0)
                                                  : 44100.0;
        reset();
        updateCoefficients();
    }

    void setTimes(float attackMilliseconds, float holdMilliseconds, float releaseMilliseconds) noexcept
    {
        attackMs = std::isfinite(attackMilliseconds)
                 ? std::clamp(attackMilliseconds, 0.001f, 10000.0f) : 10.0f;
        holdMs = std::isfinite(holdMilliseconds)
               ? std::clamp(holdMilliseconds, 0.0f, 10000.0f) : 0.0f;
        releaseMs = std::isfinite(releaseMilliseconds)
                  ? std::clamp(releaseMilliseconds, 0.001f, 10000.0f) : 100.0f;
        updateCoefficients();
    }

    float process(float targetReductionDb) noexcept
    {
        targetReductionDb = std::isfinite(targetReductionDb) ? std::max(0.0f, targetReductionDb) : 0.0f;
        if (targetReductionDb >= envelopeDb)
        {
            envelopeDb = targetReductionDb + attackCoefficient * (envelopeDb - targetReductionDb);
            holdRemaining = holdSamples;
        }
        else if (holdRemaining > 0)
        {
            --holdRemaining;
        }
        else
        {
            envelopeDb = targetReductionDb + releaseCoefficient * (envelopeDb - targetReductionDb);
        }

        if (!std::isfinite(envelopeDb) || envelopeDb < 1.0e-20f)
            envelopeDb = 0.0f;
        return envelopeDb;
    }

    void reset() noexcept { envelopeDb = 0.0f; holdRemaining = 0; }
    float current() const noexcept { return envelopeDb; }

private:
    void updateCoefficients() noexcept
    {
        const auto coefficient = [this](float milliseconds)
        {
            return static_cast<float>(std::exp(-1.0 / (0.001 * milliseconds * sampleRate)));
        };
        attackCoefficient = coefficient(attackMs);
        releaseCoefficient = coefficient(releaseMs);
        holdSamples = static_cast<int>(std::round(0.001 * holdMs * sampleRate));
    }

    double sampleRate = 44100.0;
    float attackMs = 10.0f, holdMs = 0.0f, releaseMs = 100.0f;
    float attackCoefficient = 0.0f, releaseCoefficient = 0.0f, envelopeDb = 0.0f;
    int holdSamples = 0, holdRemaining = 0;
};
}
