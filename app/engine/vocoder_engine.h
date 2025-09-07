//includes
#include "common/config.h"
#include "app/engine/biquad.h"
#include "app/engine/envelope_follower.h"

namespace recorder {

/*
    This class represents a single band of the vocoder.
    It contains an analysis and output (synthesis) IIR biquad filter
*/
class VocoderBand {

public:
    void Init(float sampleRate, float frequency) {
        //initialize the analysis filter
        analyzer.Init(sampleRate, frequency, Q, filterGain);
        //initialize the output (synthesis) filter
        outFilter.Init(sampleRate, frequency, Q, filterGain);
        //initialize the envelope follower (attack ms, decay ms, hold ms, sample rate)
        env.Init(50, 20, 20, sampleRate);
    }

    float Process(float modulatorInput, float carrierInput) {
        float out = 0.0f;

        //isolate this band of the input signal
        float inputFiltered = analyzer.Process(modulatorInput);
        //get the level of the isolated signal
        float inputLevel = env.Process(inputFiltered);
        //filter the output signal
        float carrierFiltered = outFilter.Process(carrierInput);
        //apply the level of the envelope follower to the level of the output
        out = carrierFiltered * inputLevel * makeupGain;

        return out;
    }

private:
    //the analyzer and output (synthesis) filters for this band
    Biquad analyzer;
    Biquad outFilter;

    //the envelope follower (to track the input level of this band)
    EnvelopeFollower env;

    //the Q for each filter
    static constexpr float Q = 16;
    //the gain for the biquad filter
    static constexpr float filterGain = 1;
    //the makeup gain on each vocoder band (after filtering)
    static constexpr float makeupGain = 10;
};

class Vocoder 
{

public:

    void Init() {
        //initialize all the analysis bands
        for (int i = 0; i < numBands; i++) {
            bands[i].Init(kAudioSampleRate, cutoffFreqs[i]);
            bandOutputGains[i] = log(i+1);
        }
    }

    float Process(float modulatorInput, float carrierInput) {
        
        float out = 0;

        //process each vocoder band and add to the output signal
        for (int i = 0; i < numBands; i++) {
            out += bands[i].Process(modulatorInput, carrierInput) * 0.5 * bandOutputGains[i];
        }

        return out;
    }


private:
    //there will be this many analysis bands and this many synthesis bands
    static constexpr int numBands = 8;
    //the cutoff frequencies for the analysis and synthesis filters
    float cutoffFreqs[numBands] = {300, 420, 600, 840, 1200, 1680, 2400, 3360};
    //the array that will contain all the vocoder bands
    VocoderBand bands[numBands];
    //array of gain constants that will be applied to the output of each band
    float bandOutputGains[numBands];

};


}