#include <cmath>
#include "app/engine/biquad.h"

namespace recorder {

class Lowpass : public Biquad {

public:
    void UpdateFilter()
    {
        omega = 2 * M_PI * centerFrequency_ / sampleRate_;
        alpha = sin(omega) / (2 * Q_);
        A = gain_;

        //coefficients for lowpass
        b0_ = (1.0 - cos(omega)) / 2.0;
        b1_ = 1.0 - cos(omega);
        b2_ = (1.0 - cos(omega)) / 2.0;
        a0_ = 1.0 + alpha;
        a1_ = -2.0 * cos(omega);
        a2_ = 1.0 - alpha;    

        // Scaling coefficients for unity gain at the center frequency
        b0_ /= a0_;
        b1_ /= a0_;
        b2_ /= a0_;
        a1_ /= a0_;
        a2_ /= a0_;

    }

};

}