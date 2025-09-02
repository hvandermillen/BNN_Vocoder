// jingle_engine.h
#pragma once

#include <cstdint>
#include <cmath>
#include "common/config.h"
#include "waveform_generator.h"

namespace recorder
{

    class JingleEngine
    {
    public:
        JingleEngine() = default;

        void Init()
        {
            is_active_ = false;
            current_note_ = 0;
            note_timer_ = 0;
            voice_.SetWaveform(WaveformGenerator::Waveform::SINE);
            voice_.SetFrequency(0.0f);
        }

        // Start playing the startup jingle
        void StartupJingle()
        {
            is_active_ = true;
            is_startup_ = true;
            current_note_ = 0;
            note_timer_ = 0;
            UpdateNoteFrequency();
        }

        // Start playing the ending jingle
        void EndingJingle()
        {
            is_active_ = true;
            is_startup_ = false;
            current_note_ = 0;
            note_timer_ = 0;
            UpdateNoteFrequency();
        }

        // Check if a jingle is currently active
        bool JingleActive() const
        {
            return is_active_;
        }

        // Process audio samples just like SynthEngine
        void Process(float (&block)[kAudioOSFactor])
        {
            if (!is_active_)
            {
                // If not active, output silence
                for (uint32_t i = 0; i < kAudioOSFactor; ++i)
                {
                    block[i] = 0.0f;
                }
                return;
            }

            // Update note timer and advance to next note if needed
            note_timer_++;
            if (note_timer_ >= kNoteDuration)
            {
                note_timer_ = 0;
                current_note_++;

                // Check if we've reached the end of the jingle
                const float *current_jingle = is_startup_ ? startup_jingle_ : ending_jingle_;
                int jingle_length = is_startup_ ? kStartupJingleLength : kEndingJingleLength;

                if (current_note_ >= jingle_length)
                {
                    // Jingle complete
                    is_active_ = false;
                    for (uint32_t i = 0; i < kAudioOSFactor; ++i)
                    {
                        block[i] = 0.0f;
                    }
                    return;
                }

                UpdateNoteFrequency();
            }

            // Generate the sound sample
            float sample = voice_.Process() * kVoiceScale;

            // Apply envelope to avoid clicks
            float envelope = 1.0f;
            if (note_timer_ < kFadeInSamples)
                envelope = float(note_timer_) / float(kFadeInSamples);
            else if (note_timer_ > kNoteDuration - kFadeOutSamples)
                envelope = float(kNoteDuration - note_timer_) / float(kFadeOutSamples);

            sample *= envelope;

            // Clamp and pack into oversampling buffer just like SynthEngine
            // give it a little saturation
            sample = tanh(1.5 * sample);

            sample = std::clamp(sample, -1.0f, 1.0f);
            sample *= kAudioOSFactor * kAudioOutputLevel;

            for (uint32_t i = 0; i < kAudioOSFactor; ++i)
            {
                block[i] = sample;
            }
        }

    private:
        // Update the waveform generator frequency based on current note
        void UpdateNoteFrequency()
        {
            const float *current_jingle = is_startup_ ? startup_jingle_ : ending_jingle_;
            int jingle_length = is_startup_ ? kStartupJingleLength : kEndingJingleLength;

            if (current_note_ < jingle_length)
            {
                voice_.SetFrequency(current_jingle[current_note_]);
            }
            else
            {
                voice_.SetFrequency(0.0f);
            }
        }

        // Constants for jingle playback
        static constexpr int kNoteDuration = kAudioSampleRate / 8;    // 1/4 second per note
        static constexpr int kFadeInSamples = kAudioSampleRate / 50;  // 20ms fade in
        static constexpr int kFadeOutSamples = kAudioSampleRate / 50; // 20ms fade out
        static constexpr float kVoiceScale = 0.5f;

        // Jingle note sequences (frequencies in Hz)
        static constexpr int kStartupJingleLength = 4;
        static constexpr float startup_jingle_[kStartupJingleLength] = {
            261.63f, // G4
            329.63f, // A4
            392.9f,  // B4
            587.33f, // C5
        };

        static constexpr int kEndingJingleLength = 4;
        static constexpr float ending_jingle_[kEndingJingleLength] = {
            587.3f,  // D5
            392.9f,  // C5
            329.63f, // B4
            261.63f, // A4

        };

        WaveformGenerator voice_;
        bool is_active_ = false;
        bool is_startup_ = true;
        int current_note_ = 0;
        int note_timer_ = 0;
    };

} // namespace recorder