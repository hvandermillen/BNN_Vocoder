// main.cpp
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <cinttypes>
#include <cmath>

#include "drivers/system.h"
#include "drivers/profiling.h"
#include "drivers/switches.h"
#include "drivers/analog.h"
#include "drivers/sample_memory.h"
#include "drivers/gpio.h"

#include "common/config.h"
#include "common/io.h"
#include "util/buffer_chain.h"
#include "util/edge_detector.h"
#include "monitor/monitor.h"
#include "app/engine/recording_engine.h"
#include "app/engine/playback_engine.h"

// CYCLOPS INCLUDES
#include "app/engine/synth_engine.h"
#include "app/engine/jingle_engine.h" // New include for jingle engine
// CYCLOPS INCLUDES END HERE

namespace recorder
{

    Analog analog_;
    Switches switches_;

    enum State
    {
        STATE_IDLE,
        STATE_SYNTH,
        STATE_RECORD,
        STATE_PLAY,
        STATE_STOP,
        STATE_SAVE,
        STATE_SAVE_ERASE,
        STATE_SAVE_BEGIN_WRITE,
        STATE_SAVE_WRITE,
        STATE_SAVE_COMMIT,
        STATE_STANDBY,
        STATE_STARTUP,   // New state for startup jingle
        STATE_ENDING     // New state for ending jingle
    };

    // SYNTH VARIABLES
    static constexpr int numButtons = 4;
    bool synth_inactive_ = false;
    SynthEngine synth_engine_;
    JingleEngine jingle_engine_; // New jingle engine instance
    EdgeDetector button_1_, button_2_, button_3_, button_4_;
    EdgeDetector buttons[numButtons] = {button_1_, button_2_, button_3_, button_4_};
    SwitchID buttonIDs[numButtons] = {SWITCH_KEY_1, SWITCH_KEY_2, SWITCH_KEY_3, SWITCH_KEY_4};

    std::atomic<State> state_;
    uint32_t idle_timeout_;
    uint32_t playback_timeout_;
    EdgeDetector play_button_;

    SampleMemory<__fp16> sample_memory_;
    RecordingEngine recording_{sample_memory_};
    PlaybackEngine playback_{sample_memory_};
    DeviceIO io_;
    Monitor monitor_;
    OutputPin<GPIOC_BASE, 2> ledPin;

    // track last strum pot value for movement detection
    static float last_strum_pot = 0.0f;
    // track last quantized strum postition index (0-6) for movement detection
    static int last_strum_idx = 0;
    //true if the strum pot has changed between positions (0-6), activating a strum
    bool strum_idx_changed = false;


    void Transition(State new_state)
    {
        printf("State: ");
        switch (new_state)
        {
        case STATE_IDLE:
            printf("IDLE\n");
            break;
        case STATE_SYNTH:
            printf("SYNTH\n");
            break;
        case STATE_RECORD:
            printf("RECORD\n");
            break;
        case STATE_PLAY:
            printf("PLAY\n");
            break;
        case STATE_STOP:
            printf("STOP\n");
            break;
        case STATE_SAVE:
            printf("SAVE\n");
            break;
        case STATE_SAVE_ERASE:
            printf("ERASE\n");
            break;
        case STATE_SAVE_BEGIN_WRITE:
            printf("BEGIN_WRITE\n");
            break;
        case STATE_SAVE_WRITE:
            printf("WRITE\n");
            break;
        case STATE_SAVE_COMMIT:
            printf("COMMIT\n");
            break;
        case STATE_STANDBY:
            printf("STANDBY\n");
            break;
        case STATE_STARTUP:
            printf("STARTUP\n");
            break;
        case STATE_ENDING:
            printf("ENDING\n");
            break;
        }
        state_.store(new_state, std::memory_order_acq_rel);
    }

    void StateMachine(bool standby)
    {
        // Refresh inputs
        switches_.Process(io_.human.in);
        play_button_.Process(io_.human.in.sw[SWITCH_PLAY]);

        // Read strum pot and detect movement
        float strum_pot = io_.human.in.pot[POT_2];
        // consider movement significant above threshold
        // bool strum_moved = fabsf(strum_pot - last_strum_pot) > 0.00001f;          
        last_strum_pot = strum_pot;
        //also update index of strum position
        int strum_idx = int(strum_pot * 5.99f); // 0-5 for 6 strum positions 
        strum_idx_changed = (strum_idx != last_strum_idx); //did we change positions
        last_strum_idx = strum_idx;
        //if we are holding the record button, we are recording
        //maybe change record and playback to one button? hold vs tap?
        bool record = io_.human.in.sw[SWITCH_RECORD];

        // Process each key button
        for (int i = 0; i < numButtons; ++i)
            buttons[i].Process(io_.human.in.sw[buttonIDs[i]]);

        State cur = state_.load(std::memory_order_relaxed);

        // Handle jingle states
        if (cur == STATE_STARTUP)
        {
            // Check if startup jingle is still playing
            if (!jingle_engine_.JingleActive())
            {
                // Jingle finished, transition to idle
                analog_.MutePowerStage();
                synth_inactive_ = true;   // Set the inactive flag
                
                Transition(STATE_IDLE);
                
            }
            return;
        }
        else if (cur == STATE_ENDING)
        {
            // Check if ending jingle is still playing
            if (!jingle_engine_.JingleActive())
            {
                // Jingle finished, transition to standby
                Transition(STATE_STANDBY);
            }
            return;
        }

        // Handle external standby request
        if (standby)
        {
            // Instead of going directly to standby, go to ending
            analog_.Start(true); // Ensure audio is on for jingle
            jingle_engine_.EndingJingle();
            Transition(STATE_ENDING);
            return;
        }

        if (cur == STATE_IDLE)
        {
            // Wake on any key, or strum move
            if (buttons[0].is_high() || buttons[1].is_high() || buttons[2].is_high() || strum_idx_changed)
            {
                // Start audio+ADC
                analog_.Start(true);
                idle_timeout_ = 0; // Reset timeout on activity
                Transition(STATE_SYNTH);
            }
            //reset playback timeout when we press the play button
            else if (play_button_.is_high()) {
                playback_timeout_ = 0;
            }
            else if (kEnableIdleStandby &&
                     ++idle_timeout_ > kIdleStandbyTime * 1000)
            {
                // After inactivity, transition to ending instead of directly to standby
                idle_timeout_ = 0;
                analog_.Start(true); // Ensure audio is on for jingle
                jingle_engine_.EndingJingle();
                Transition(STATE_ENDING);
            }
        }
        else if (cur == STATE_SYNTH)
        {
            // Reset idle timeout on any activity
            if (button_1_.is_high() || button_2_.is_high() || 
                button_3_.is_high() || button_4_.is_high() || strum_idx_changed)
            {
                idle_timeout_ = 0;
            }
            
            if (synth_inactive_)
            {
                // Wake on key or strum
                bool anyKey = false;
                //don't include button 4
                for (int i = 0; i < numButtons-1; ++i)
                    if (buttons[i].is_high())
                    {
                        anyKey = true;
                        break;
                    }

                if (anyKey || strum_idx_changed || play_button_.is_high())
                {
                    analog_.Start(true);
                    synth_inactive_ = false;
                    idle_timeout_ = 0; // Reset timeout on activity
                }
                else if (kEnableIdleStandby &&
                         ++idle_timeout_ > kIdleStandbyTime * 1000)
                {
                    // Transition to ending instead of directly to standby
                    analog_.Start(true); // Ensure audio is on for jingle
                    jingle_engine_.EndingJingle();
                    Transition(STATE_ENDING);
                }
            }

            static uint32_t synthReleaseCounter = 0;
            if (!synth_engine_.getActive())
            {
                if (++synthReleaseCounter >= 10)
                {
                    // Stop audio, keep ADC on for pot updates
                    analog_.MutePowerStage();
                    synth_inactive_ = true;
                    synthReleaseCounter = 0;
                }
            }
            else
            {
                synthReleaseCounter = 0;
            }
        }

        else if (cur == STATE_RECORD)
        {
            ledPin.Write(1);
            if (!record)
            {
                analog_.Stop();
                Transition(STATE_IDLE);
                sample_memory_.StopRecording();
            }
        }
        else if (cur == STATE_PLAY)
        {
            ledPin.Write(1);
            if (analog_.running())
            {
                if ((++playback_timeout_ == kPlaybackExpireTime * 1000) ||
                    (play_button_.rising() && playback_.playing()))
                {
                    playback_.Stop();
                }
                else if (play_button_.rising() && playback_.stopping())
                {
                    playback_.Play();
                }
                else if (playback_.ended())
                {
                    analog_.Stop();
                }
            }
            else if (analog_.stopped())
            {
                Transition(STATE_STOP);
            }
        }
        else if (cur == STATE_STOP)
        {
            if (play_button_.is_low())
            {
                Transition(STATE_IDLE);
            }
        }
        else if (cur == STATE_SAVE)
        {
            if (sample_memory_.dirty())
            {
                if (sample_memory_.BeginErase())
                {
                    Transition(STATE_SAVE_ERASE);
                }
                else
                {
                    printf("Erase failed\n");
                    Transition(STATE_STANDBY);
                }
            }
            else
            {
                Transition(STATE_STANDBY);
            }
        }
        else if (cur == STATE_SAVE_ERASE)
        {
            if (sample_memory_.FinishErase())
            {
                Transition(STATE_SAVE_BEGIN_WRITE);
            }
            else if (record || play_button_.is_high())
            {
                printf("Save aborted\n");
                sample_memory_.AbortErase();
                Transition(STATE_IDLE);
            }
        }
        else if (cur == STATE_SAVE_BEGIN_WRITE)
        {
            if (sample_memory_.write_complete())
            {
                Transition(STATE_SAVE_COMMIT);
            }
            else if (sample_memory_.BeginWrite())
            {
                Transition(STATE_SAVE_WRITE);
            }
            else
            {
                printf("Write failed\n");
                Transition(STATE_STANDBY);
            }
        }
        else if (cur == STATE_SAVE_WRITE)
        {
            if (sample_memory_.FinishWrite())
            {
                Transition(STATE_SAVE_BEGIN_WRITE);
            }
            else if (record || play_button_.is_high())
            {
                printf("Save aborted\n");
                sample_memory_.AbortWrite();
                Transition(STATE_IDLE);
            }
        }
        else if (cur == STATE_SAVE_COMMIT)
        {
            if (sample_memory_.Commit())
            {
                printf("Save completed\n");
                sample_memory_.PrintInfo("    ");
            }
            else
            {
                printf("Commit failed\n");
            }

            Transition(STATE_STANDBY);
        }

        else if (cur == STATE_STANDBY)
        {
            system::SerialFlushTx();
            analog_.Stop();
            sample_memory_.PowerDown();
            system::Standby();
            // System will reset after wakeup, so code beyond this point won't execute
            // The actual startup transition happens in main() after system wakeup
        }
    }

    const AudioOutput Process(const AudioInput &audio_in, const PotInput &pot)
    {
        ScopedProfilingPin<PROFILE_PROCESS> profile;
        io_.human.in.pot = pot;

        AudioOutput audio_out = {};
        State cur = state_.load(std::memory_order_acquire);

        if (cur == STATE_SYNTH)
        {
            bool synth_buttons[numButtons];
            
            // Use first 3 synth buttons normally
            for (int i = 0; i < numButtons; ++i)
                synth_buttons[i] = buttons[i].is_high();
            
            // Button 4 (index 3) is now what play_button was
            synth_buttons[3] = play_button_.is_high();

            float chord_pot = pot[POT_5];
            float strum = pot[POT_2];
            float hold = pot[POT_1];
            bool mode = io_.human.in.sw[SWITCH_LOOP];
            
            // Use button_4 for seventh parameter instead of play_button
            bool seventh = buttons[3].is_high();
            bool minor_seventh = io_.human.in.sw[SWITCH_RECORD];

            synth_engine_.Process(
                audio_out[AUDIO_OUT_LINE],
                synth_buttons,
                chord_pot, hold, last_strum_idx, strum_idx_changed,
                mode, seventh, minor_seventh);
        }
        else if (cur == STATE_STARTUP || cur == STATE_ENDING)
        {
            // Process jingle audio
            jingle_engine_.Process(audio_out[AUDIO_OUT_LINE]);
        }

        return audio_out;
    }

    extern "C" int main(void)
    {
        system::Init();
        ProfilingPin<PROFILE_MAIN>::Set();

        analog_.Init(Process);
        switches_.Init();
        play_button_.Init();
        button_1_.Init();
        button_2_.Init();
        button_3_.Init();
        button_4_.Init();

        analog_.StartPlayback();
        recording_.Init();
        playback_.Init();
        synth_engine_.Init();
        jingle_engine_.Init(); // Initialize jingle engine
        io_.Init();
        monitor_.Init();
        playback_.Reset();
        sample_memory_.Init();
        
        // Start with startup jingle instead of directly to synth
        analog_.Start(true); // Ensure audio is on for jingle
        jingle_engine_.StartupJingle();
        Transition(STATE_STARTUP);
        
        // Initialize idle timeout counter
        idle_timeout_ = 0;

        bool expire_watchdog = false;
        if (kADCAlwaysOn)
            analog_.Start(false);

        for (;;)
        {
            ProfilingPin<PROFILE_MAIN_LOOP>::Set();
            std::atomic_thread_fence(std::memory_order_acq_rel);

            bool standby = false;
            auto message = monitor_.Receive();
            if (message.type == Message::TYPE_QUERY)
                monitor_.Report(io_);
            else if (message.type == Message::TYPE_STANDBY)
                standby = true;
            else if (message.type == Message::TYPE_WATCHDOG)
                expire_watchdog = true;
            else if (message.type == Message::TYPE_RESET)
            {
                system::SerialFlushTx();
                system::Reset();
            }
            else if (message.type == Message::TYPE_ERASE)
                sample_memory_.Erase();

            if (!expire_watchdog)
                system::ReloadWatchdog();

            StateMachine(standby);
            ProfilingPin<PROFILE_MAIN_LOOP>::Clear();
            system::Delay_ms(1);
        }
    }
} // namespace recorder