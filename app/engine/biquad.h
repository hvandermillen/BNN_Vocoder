#pragma once
#include <cmath>

namespace recorder
{

class Biquad
{
public:
    void Init(bool is_bandpass, float sampleRate, float centerFrequency, float Q, float gainDB)
    {
        is_bandpass_ = is_bandpass;
        sampleRate_ = sampleRate;
        SetParameters(centerFrequency, Q, gainDB);

    }

    void SetParameters(float centerFrequency, float Q, float gainDB)
    {
        centerFrequency_ = centerFrequency;
        Q_ = Q;
        gain_ = std::pow(10, gainDB / 20.0); // Convert gain from dB to linear scale

        x1_ = x2_ = y1_ = y2_ = 0;

        UpdateFilter();
    }

    float Process(float input)
    {
        // Direct Form I implementation of the IIR filter
        float y0 = b0_ * input + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;

        // Shift the delay line
        x2_ = x1_;
        x1_ = input;
        y2_ = y1_;
        y1_ = y0;

        return y0;
    }

protected:
    void UpdateFilter()
    {
        omega = 2 * M_PI * centerFrequency_ / sampleRate_;
        alpha = sin(omega) / (2 * Q_);
        A = gain_;

        if (!is_bandpass_)
        {
            //coefficients for lowpass
            b0_ = 1 + alpha * A;
            b1_ = -2 * cos(omega);
            b2_ = 1 - alpha * A;
            a0_ = 1 + alpha / A;
            a1_ = -2 * cos(omega);
            a2_ = 1 - alpha / A;

        } else {
            //coefficients for bandpass
            b0_ =   alpha;
            b1_ =   0.0;
            b2_ =  -alpha;
            a0_ =   1.0 + alpha;
            a1_ =  -2.0 * cos(omega);
            a2_ =   1.0 - alpha;
        }

        // Scaling coefficients for unity gain at the center frequency
        b0_ /= a0_;
        b1_ /= a0_;
        b2_ /= a0_;
        a1_ /= a0_;
        a2_ /= a0_;

    }

    //true if the filter is a bandpass and now lowpass
    bool is_bandpass_;

    float sampleRate_;
    float centerFrequency_;
    float Q_;
    float gain_;

    float a0_, a1_, a2_;
    float b0_, b1_, b2_;
    float x1_, x2_;
    float y1_, y2_;

    float omega, alpha, A;
};

}
