// waveform_generator.h
#pragma once

#include <cmath>
#include "common/config.h"

namespace recorder {

class WaveformGenerator {
public:
    enum class Waveform { SINE, TRIANGLE };

    WaveformGenerator()
      : phase_(0.0f),
        phase_inc_(0.0f),
        waveform_(Waveform::SINE)
    {}

    inline void SetFrequency(float freq) {
        phase_inc_ = freq * kPhaseFactor;
    }
    inline void SetWaveform(Waveform w) {
        waveform_ = w;
    }

    inline float Process() {
        float out;
        if (waveform_ == Waveform::SINE) {
            out = sinf(phase_);
        } else {
            // TRIANGLE
            // normalize φ = phase_/π in [0,2)
            float phi = phase_ * kInvPi;
            float tri = (phi < 1.0f)
                      ? phi
                      : (phi < 2.0f ? 2.0f - phi : 0.0f);
            out = tri * 2.0f - 1.0f;
        }
        phase_ += phase_inc_;
        if (phase_ >= kTwoPi) phase_ -= kTwoPi;
        return out * kOutputScale;
    }

private:
    float            phase_;
    float            phase_inc_;
    Waveform         waveform_;

    static constexpr float kTwoPi        = 2.0f * (float)M_PI;
    static constexpr float kInvPi        = 1.0f / (float)M_PI;
    static constexpr float kPhaseFactor  = kTwoPi / (float)kAudioSampleRate;
    static constexpr float kOutputScale  = 0.08f;
};

} // namespace recorder
