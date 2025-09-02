#pragma once

#include <cstdint>
#include <cmath>
#include "common/config.h"
#include "app/engine/aafilter.h"
#include "waveform_generator.h"

namespace recorder
{

class SynthEngine
{
    enum { kNumVoices = 4, kNumChords = 8, kNumStrum = 6 }; // Changed kNumStrum to 6

public:
    SynthEngine() = default;

    void Init()
    {
        mode_ = false;
        current_chord_ = 0;
        base_frequency_ = 261.63f; // Default to middle C
        in_base_freq_mode_ = false;
        seventh_hold_counter_ = 0;

        // Main voices (triangle)
        for (int v = 0; v < kNumVoices; ++v)
        {
            current_freq_[v] = 0.0f;
            target_freq_[v] = 0.0f;
            voices_[v].SetWaveform(WaveformGenerator::Waveform::TRIANGLE);
            voices_[v].SetFrequency(0.0f);
            env_state_[v] = ENV_IDLE;
            env_level_[v] = 0.0f;
            gate_[v] = false;
        }

        // Strum voices (sine)
        for (int s = 0; s < kNumStrum; ++s)
        {
            strum_current_[s] = 0.0f;
            strum_target_[s] = 0.0f;
            strum_voices_[s].SetWaveform(WaveformGenerator::Waveform::SINE);
            strum_voices_[s].SetFrequency(0.0f);
            strum_state_[s] = ENV_IDLE;
            strum_level_[s] = 0.0f;
            strum_activation_time_[s] = 0;
            strum_attenuation_[s] = 1.0f;
        }

        last_strum_ = -1;
        strum_activation_counter_ = 0;
        aa_filter_.Init();

        // Compressor init
        compEnv_ = 0.0f;
        compGain_ = 1.0f;
        alphaAtk_ = std::exp(-1.0f/(kCompAttackTime * kAudioSampleRate));
        alphaRel_ = std::exp(-1.0f/(kCompReleaseTime* kAudioSampleRate));

        updateChordTargets(false, false);
    }

    //used for note setting
    float Min(float a, float b) {
        if (a < b) {
            return a;
        }
        return b;
    }

    // Set the base frequency (root note of the scale)
    void SetBaseFrequency(float freq)
    {
        base_frequency_ = freq;
        updateChordTargets(false, false);
    }

    // Get the current base frequency
    float GetBaseFrequency() const
    {
        return base_frequency_;
    }

    // mode = false → major scale, true → minor scale
    // major7: apply major seventh; minor7: apply minor seventh
    // if both major7 and minor7: apply major sixth
    void Process(float (&block)[kAudioOSFactor],
                 const bool button[kNumVoices],
                 float chord_pot,
                 float hold_pot,
                 int strum_idx,
                 int strum_idx_changed,
                 bool mode,
                 bool major7,
                 bool minor7)
    {
        // Check for entering/exiting base frequency mode
        if (major7 && minor7) {
            seventh_hold_counter_++;
            if (seventh_hold_counter_ >= kSeventhHoldCycles) {
                in_base_freq_mode_ = true;
                // Force first voice on, others off
                for (int v = 0; v < kNumVoices; ++v) {
                    if (v == 0) {
                        env_state_[v] = ENV_ATTACK;
                    } else {
                        env_state_[v] = ENV_RELEASE;
                    }
                }
            }
        } else {
            seventh_hold_counter_ = 0;
            if (in_base_freq_mode_) {
                in_base_freq_mode_ = false;
                // Return to normal operation
                updateChordTargets(major7, minor7);
            }
        }

        if (!in_base_freq_mode_) {
            // Normal operation mode
            
            // mode switch?
            if (mode != mode_) { mode_ = mode; }

            // chord change
            int chord_idx = int(Min(chord_pot, 0.9999f) * (float)(kNumChords - 1)) + (chord_pot >= 0.9999f);
            if (chord_idx != current_chord_)
            {
                current_chord_ = chord_idx;
            }

            // update targets based on chord, mode, and 7th/6th flags
            updateChordTargets(major7, minor7);

            // strum trigger (now 6 positions for 6 voices)
            
            if (strum_idx_changed)
            { 
                
                last_strum_ = strum_idx;
                
                // Direct voice mapping - no cycling needed
                int voice_idx = strum_idx;
                
                // Calculate the target frequency immediately
                int idx = strum_idx % kNumVoices;
                int oct = strum_idx / kNumVoices;
                const float* scale_multipliers = mode_ ? minor_scale_multipliers_ : major_scale_multipliers_;
                const int* chord_types = mode_ ? minor_scale_chord_types_ : major_scale_chord_types_;
                float root_freq = base_frequency_ * scale_multipliers[current_chord_];
                int chord_type = chord_types[current_chord_];
                const float* chord_multipliers;
                
                switch (chord_type) {
                    case 0: chord_multipliers = major_chord_multipliers_; break;
                    case 1: chord_multipliers = minor_chord_multipliers_; break;
                    case 2: chord_multipliers = diminished_chord_multipliers_; break;
                    default: chord_multipliers = major_chord_multipliers_;
                }
                
                float note = root_freq * chord_multipliers[idx];
                float target_note = note * (1 << oct);
                
                // Set current frequency to the target to avoid sudden changes
                strum_current_[voice_idx] = target_note;
                strum_target_[voice_idx] = target_note;
                strum_voices_[voice_idx].SetFrequency(target_note);
                
                // Start envelope from 0 to prevent clicks
                strum_level_[voice_idx] = 0.0f;
                strum_state_[voice_idx] = ENV_ATTACK;
                strum_activation_time_[voice_idx] = ++strum_activation_counter_;
                strum_attenuation_[voice_idx] = 1.0f;
                
                // Update frequencies for all active voices (if chord/mode changed)
                updateStrum();
                
                // Update attenuation factors for all active voices
                updateStrumAttenuation();
                
            }
            
        } else {
            // Base frequency selection mode
            int chromatic_idx = int(chord_pot * 12.99f); // 0-12 for C4-C5
            base_frequency_ = chromatic_frequencies_[chromatic_idx];
            
            // Only first voice plays the base frequency
            target_freq_[0] = base_frequency_;
            
            // Turn off all other voices
            for (int v = 1; v < kNumVoices; ++v) {
                target_freq_[v] = 0.0f;
            }
            
            // No strum activation in base freq mode
            last_strum_ = -1;
        }

        // slew main freqs
        for (int v = 0; v < kNumVoices; ++v)
        {
            slew(current_freq_[v], target_freq_[v], kFreqSlew);
            current_freq_[v] = target_freq_[v];
            voices_[v].SetFrequency(current_freq_[v]);
        }

        // slew strum freqs
        for (int s = 0; s < kNumStrum; ++s)
        {
            slew(strum_current_[s], strum_target_[s], kStrumFreqSlew);
            strum_voices_[s].SetFrequency(strum_current_[s]);
        }

        // 5) gates → envelopes (hold=1 → infinite sustain)
        for (int v = 0; v < kNumVoices; ++v)
        {
            bool g = button[v];
            if (in_base_freq_mode_ && v != 0) {
                // In base frequency mode, all voices except first are off
                g = false;
            }
            if (g && !gate_[v])
                env_state_[v] = ENV_ATTACK;
            else if (!g && gate_[v])
                env_state_[v] = (hold_pot >= 0.999f ? ENV_SUSTAIN : ENV_RELEASE);
            gate_[v] = g;
        }

        // 6) dynamic release via exp2 for buttons
        float releaseTime = kMinRelTime * exp2f(hold_pot * kRelLog2Ratio);
        float relInc = 1.0f / (releaseTime * kAudioSampleRate);
        
        // Dynamic release for strum voices
        float strumReleaseTime = kStrumMinRelTime * exp2f(hold_pot * kStrumRelLog2Ratio);
        float strumRelInc = 1.0f / (strumReleaseTime * kAudioSampleRate);

        // if knob just turned down, force release
        if (hold_pot < 0.999f)
            for (int v = 0; v < kNumVoices; ++v)
                if (env_state_[v] == ENV_SUSTAIN && !gate_[v])
                    env_state_[v] = ENV_RELEASE;

        // 7) mix voices
        float mix = 0.0f;
        for (int v = 0; v < kNumVoices; ++v)
        {
            switch (env_state_[v])
            {
                case ENV_ATTACK:
                    env_level_[v] += kAttackInc;
                    if (env_level_[v] >= 1.0f)
                        { env_level_[v] = 1.0f; env_state_[v] = ENV_DECAY; }
                    break;
                case ENV_DECAY:
                    env_level_[v] -= kDecayInc;
                    if (env_level_[v] <= kSustain)
                        { env_level_[v] = kSustain; env_state_[v] = ENV_SUSTAIN; }
                    break;
                case ENV_RELEASE:
                    env_level_[v] -= relInc;
                    if (env_level_[v] <= 0.0f)
                        { env_level_[v] = 0.0f; env_state_[v] = ENV_IDLE; }
                    break;
                default: break;
            }
            if (env_state_[v] != ENV_IDLE)
                mix += voices_[v].Process() * env_level_[v] * kVoiceScale;
        }

        // strum mix with dynamic release and attenuation
        if (!in_base_freq_mode_) {
            for (int s = 0; s < kNumStrum; ++s)
            {
                switch (strum_state_[s])
                {
                    case ENV_ATTACK:
                        strum_level_[s] += kAttackInc;
                        if (strum_level_[s] >= 1.0f)
                            { strum_level_[s] = 1.0f; strum_state_[s] = ENV_DECAY; }
                        break;
                    case ENV_DECAY:
                        strum_level_[s] -= kDecayInc;
                        if (strum_level_[s] <= kSustain)
                            { strum_level_[s] = kSustain; strum_state_[s] = ENV_SUSTAIN; }
                        break;
                    case ENV_SUSTAIN:
                        strum_level_[s] -= strumRelInc;
                        if (strum_level_[s] <= 0.0f)
                        { 
                            strum_level_[s] = 0.0f; 
                            strum_state_[s] = ENV_IDLE; 
                            
                            // Update attenuation whenever a voice becomes idle
                            updateStrumAttenuation();
                        }
                        else
                        {
                            // Apply both envelope level and dynamic attenuation
                            mix += strum_voices_[s].Process() * strum_level_[s] * strum_attenuation_[s] * kVoiceScale;
                        }
                        break;
                    default: break;
                }
            }
        }
        
        // apply dynamic compressor (NYC style)
        float dry = mix;
        float wet = ApplyCompressor(mix);
        mix = dry * 0.3f + wet * .7f;

        // give it a little saturation
        mix = tanh(2.5 * mix);
        
        // 8) oversample‑pack
        mix *= kAudioOSFactor * kAudioOutputLevel;
        mix = std::clamp(mix, -1.0f, 1.0f);
        for (uint32_t i = 0; i < kAudioOSFactor; ++i){
           // block[i] = aa_filter_.Process(i == 0 ? mix : 0.0f);
           block[i] = mix;
        }
    }

    bool getActive() const
    {
        for (int v = 0; v < kNumVoices; ++v)
            if (env_state_[v] != ENV_IDLE) return true;
        for (int s = 0; s < kNumStrum; ++s)
            if (strum_state_[s] != ENV_IDLE) return true;
        return false;
    }

private:
    // ADSR & constants - made attack slower to reduce clicks
    static constexpr float kAttackTime   = 0.02f;  // Changed from 0.01f to 0.02f
    static constexpr float kDecayTime    = 0.05f;
    static constexpr float kSustain      = 0.70f;
    static constexpr float kMinRelTime   = 0.005f;
    static constexpr float kMaxRelTime   = 10.0f;
    static constexpr float kRelLog2Ratio = std::log2(kMaxRelTime/kMinRelTime);

    // Strum release constants
    static constexpr float kStrumMinRelTime = 0.05f;
    static constexpr float kStrumMaxRelTime = 1.9f;
    static constexpr float kStrumRelLog2Ratio = std::log2(kStrumMaxRelTime/kStrumMinRelTime);

    // Updated attenuation levels array for 5 older voices when all 6 are active
    static constexpr float kAttenuationLevels[5] = { 0.9f, 0.8f, 0.7f, 0.6f, 0.5f };

    static constexpr float kAttackInc    = 1.0f/(kAttackTime*kAudioSampleRate);
    static constexpr float kDecayInc     = (1.0f-kSustain)/(kDecayTime*kAudioSampleRate);
    static constexpr float kFreqSlew     = 0.3f;
    static constexpr float kStrumFreqSlew = 0.5f; // Faster slew for strum frequencies
    static constexpr float kVoiceScale   = 0.25;
    static constexpr float kSynthGain    = 1.0f;

    // Compressor state & params
    float compEnv_{0.0f};       // current detected envelope
    float compGain_{1.0f};      // current gain multiplier
    float alphaAtk_{0.0f}, alphaRel_{0.0f}; // filter coeffs

    static constexpr float kCompThreshold   = 1.0f;
    static constexpr float kCompAttackTime  = 0.000001f;
    static constexpr float kCompReleaseTime = 0.200f;

    // Seventh and sixth ratios
    static constexpr float kMinor7Ratio = 1.781797f; // 2^(10/12)
    static constexpr float kMajor7Ratio = 1.887749f; // 2^(11/12)
    static constexpr float kMajor6Ratio = 1.681793f; // 2^(9/12)

    enum EnvelopeState { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

    // Base frequency (default to middle C)
    float base_frequency_ = 261.63f;

    // Base frequency mode control
    bool in_base_freq_mode_ = false;
    int seventh_hold_counter_ = 0;
    static constexpr int kSeventhHoldCycles = 100;  // Cycles to hold before entering freq mode

    // Chromatic scale from C4 to C5 (13 notes inclusive)
    static constexpr float chromatic_frequencies_[13] = {
        261.63f,  // C4
        277.18f,  // C#4
        293.66f,  // D4
        311.13f,  // D#4
        329.63f,  // E4
        349.23f,  // F4
        369.99f,  // F#4
        392.00f,  // G4
        415.30f,  // G#4
        440.00f,  // A4
        466.16f,  // A#4
        493.88f,  // B4
        523.25f   // C5
    };

    // Scale degree multipliers (relative to the root)
    static constexpr float major_scale_multipliers_[kNumChords] = {
        1.0f,            // I   - root
        1.122462f,       // ii  - major 2nd
        1.259921f,       // iii - major 3rd
        1.334840f,       // IV  - perfect 4th
        1.498307f,       // V   - perfect 5th
        1.681793f,       // vi  - major 6th
        1.887749f,       // vii - major 7th
        2.0f             // VIII - octave
    };

    static constexpr float minor_scale_multipliers_[kNumChords] = {
        1.0f,            // i   - root
        1.122462f,       // ii  - major 2nd
        1.189207f,       // iii - minor 3rd
        1.334840f,       // iv  - perfect 4th
        1.498307f,       // v   - perfect 5th
        1.587401f,       // vi  - minor 6th
        1.781797f,       // vii - minor 7th
        2.0f             // viii - octave
    };

    // Chord type multipliers (relative to chord root)
    static constexpr float major_chord_multipliers_[kNumVoices] = {
        1.0f,            // root
        1.259921f,       // major 3rd
        1.498307f,       // perfect 5th
        2.0f             // octave
    };

    static constexpr float minor_chord_multipliers_[kNumVoices] = {
        1.0f,            // root
        1.189207f,       // minor 3rd
        1.498307f,       // perfect 5th
        2.0f             // octave
    };

    static constexpr float diminished_chord_multipliers_[kNumVoices] = {
        1.0f,            // root
        1.189207f,       // minor 3rd
        1.414214f,       // diminished 5th
        2.0f             // octave
    };

    // Diatonic chord types for major and minor scales
    // 0 = major, 1 = minor, 2 = diminished
    static constexpr int major_scale_chord_types_[kNumChords] = {
        0, 1, 1, 0, 0, 1, 2, 0  // I, ii, iii, IV, V, vi, vii°, I'
    };

    static constexpr int minor_scale_chord_types_[kNumChords] = {
        1, 2, 0, 1, 1, 0, 0, 1  // i, ii°, III, iv, v, VI, VII, i'
    };

    WaveformGenerator voices_[kNumVoices];
    float current_freq_[kNumVoices], target_freq_[kNumVoices];
    EnvelopeState env_state_[kNumVoices];
    float env_level_[kNumVoices];
    bool gate_[kNumVoices];

    WaveformGenerator strum_voices_[kNumStrum];
    float strum_current_[kNumStrum], strum_target_[kNumStrum];
    EnvelopeState strum_state_[kNumStrum];
    float strum_level_[kNumStrum];
    int last_strum_;
    
    // Voice tracking and attenuation
    uint32_t strum_activation_time_[kNumStrum];
    uint32_t strum_activation_counter_;
    float strum_attenuation_[kNumStrum];  // Attenuation factor for each voice

    AAFilter<float> aa_filter_;
    int current_chord_;
    bool mode_;

    static inline void slew(float &c, float t, float r)
    {
        float d = t - c;
        c += (d > 0 ? r : -r) * (fabsf(d) > r);
    }

    inline float ApplyCompressor(float in)
    {
        float absIn = fabsf(in);
        if (absIn > compEnv_)
            compEnv_ = alphaAtk_ * compEnv_ + (1 - alphaAtk_) * absIn;
        else
            compEnv_ = alphaRel_ * compEnv_ + (1 - alphaRel_) * absIn;
        float targetGain = (compEnv_ > kCompThreshold)
                            ? (kCompThreshold / compEnv_)
                            : 1.0f;
        if (targetGain < compGain_)
            compGain_ = alphaAtk_ * compGain_ + (1 - alphaAtk_) * targetGain;
        else
            compGain_ = alphaRel_ * compGain_ + (1 - alphaRel_) * targetGain;
        return in * compGain_;
    }

    inline void updateStrumAttenuation()
    {
        // First, count active voices and collect their indices and activation times
        struct VoiceInfo { int index; uint32_t activation_time; };
        VoiceInfo active_voices[kNumStrum];
        int active_count = 0;
        
        for (int s = 0; s < kNumStrum; ++s)
        {
            if (strum_state_[s] != ENV_IDLE)
            {
                active_voices[active_count].index = s;
                active_voices[active_count].activation_time = strum_activation_time_[s];
                active_count++;
            }
        }
        
        // Set default attenuation to 1.0 (no attenuation)
        for (int s = 0; s < kNumStrum; ++s)
        {
            strum_attenuation_[s] = 1.0f;
        }
        
        // If we have at least 2 active voices, apply attenuation to all but the most recent
        if (active_count >= 2)
        {
            // Sort active voices by activation time (bubble sort - efficient for small arrays)
            for (int i = 0; i < active_count - 1; i++)
            {
                for (int j = 0; j < active_count - i - 1; j++)
                {
                    if (active_voices[j].activation_time > active_voices[j + 1].activation_time)
                    {
                        // Swap
                        VoiceInfo temp = active_voices[j];
                        active_voices[j] = active_voices[j + 1];
                        active_voices[j + 1] = temp;
                    }
                }
            }
            
            // Apply attenuation to all except the most recent voice
            // The last element (active_count-1) is the most recent, so we attenuate from 0 to active_count-2
            for (int i = 0; i < active_count - 1; i++)
            {
                int voice_idx = active_voices[i].index;
                // Use the attenuation levels array, clamping to the available levels
                int att_idx = active_count - 2 - i; // Map to attenuation array (reverse order)
                if (att_idx >= 5) att_idx = 4;      // Clamp to array bounds (5 elements now)
                if (att_idx < 0) att_idx = 0;
                strum_attenuation_[voice_idx] = kAttenuationLevels[att_idx];
            }
        }
    }

    inline void updateStrum()
    {
        // Check all voices and update frequencies for active ones
        for (int s = 0; s < kNumStrum; ++s)
        {
            if (strum_state_[s] != ENV_IDLE)
            {
                // Each voice is permanently assigned to its index
                int strum_idx = s;
                
                // Calculate frequency based on strum index
                int idx = strum_idx % kNumVoices;
                int oct = strum_idx / kNumVoices;
                
                // Get the root frequency for this chord
                const float* scale_multipliers = mode_ ? minor_scale_multipliers_ : major_scale_multipliers_;
                const int* chord_types = mode_ ? minor_scale_chord_types_ : major_scale_chord_types_;
                float root_freq = base_frequency_ * scale_multipliers[current_chord_];
                
                // Get chord multipliers
                int chord_type = chord_types[current_chord_];
                const float* chord_multipliers;
                
                switch (chord_type) {
                    case 0: chord_multipliers = major_chord_multipliers_; break;
                    case 1: chord_multipliers = minor_chord_multipliers_; break;
                    case 2: chord_multipliers = diminished_chord_multipliers_; break;
                    default: chord_multipliers = major_chord_multipliers_;
                }
                
                // Calculate note
                float note = root_freq * chord_multipliers[idx];
                
                // Apply octave shift
                strum_target_[s] = note * (1 << oct);
            }
        }
    }

    inline void updateChordTargets(bool major7, bool minor7)
    {
        // Get the scale multipliers based on mode
        const float* scale_multipliers = mode_ ? minor_scale_multipliers_ : major_scale_multipliers_;
        const int* chord_types = mode_ ? minor_scale_chord_types_ : major_scale_chord_types_;
        
        // Get the root frequency for this chord
        float root_freq = base_frequency_ * scale_multipliers[current_chord_];
        
        // Determine chord type (major, minor, diminished)
        int chord_type = chord_types[current_chord_];
        const float* chord_multipliers;
        
        switch (chord_type) {
            case 0: // Major
                chord_multipliers = major_chord_multipliers_;
                break;
            case 1: // Minor
                chord_multipliers = minor_chord_multipliers_;
                break;
            case 2: // Diminished
                chord_multipliers = diminished_chord_multipliers_;
                break;
            default:
                chord_multipliers = major_chord_multipliers_;
        }
        
        // Calculate frequencies for the first three voices
        target_freq_[0] = root_freq * chord_multipliers[0];
        target_freq_[1] = root_freq * chord_multipliers[1];
        target_freq_[2] = root_freq * chord_multipliers[2];
        
        // Calculate the fourth voice based on seventh flags
        if (major7 && !minor7)
            target_freq_[3] = root_freq * kMajor7Ratio;
        else if (minor7 && !major7)
            target_freq_[3] = root_freq * kMinor7Ratio;
        else if (major7 && minor7)
            target_freq_[3] = root_freq * kMajor6Ratio;
        else
            target_freq_[3] = root_freq * chord_multipliers[3]; // Default to the octave
        
        // Update strum frequencies if needed
        updateStrum();
    }
};

} // namespace recorder