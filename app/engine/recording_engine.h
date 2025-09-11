#pragma once

#include <cstdint>
#include <cmath>

#include "common/config.h"
#include "app/engine/resampler.h"
#include "app/engine/aafilter.h"

namespace recorder
{

template <typename T>
class RecordingEngine
{
public:
    RecordingEngine(T& memory) : memory_{memory} {}

    void Init(void)
    {
        resampler_.Init();
        aa_filter_.Init();
        Reset();
    }

    void Reset(void)
    {
        resampler_.Reset();
        aa_filter_.Reset();
        sampleSum = 0;
    }

    void Process(const float (&block)[kAudioOSFactor], float pitch)
    {
        float ratio = std::exp2(pitch);
        float sample = 0;

        for (uint32_t i = 0; i < kAudioOSFactor; i++)
        {
            sample = aa_filter_.Process(block[i]);
        }

        resampler_.Push(sample, ratio);

        while (resampler_.Pop(sample))
        {
            sampleSum += sample;
            numSamples++;
            memory_.Append(sample);
        }
    }

    float AverageLevel() {
        return (float)(sampleSum/ ((double)(numSamples)) );
    }

protected:
    T& memory_;
    Resampler<16> resampler_;
    AAFilter<float> aa_filter_;

    //for calculating average level
    double sampleSum;
    long numSamples;
};

}
