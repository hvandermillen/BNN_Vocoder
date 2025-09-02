# BrandNewNoise Chord-Synth

This repository contains the implementation of the BrandNewNoise chord-synth, a synthesizer project focused on chord generation and sound synthesis.

## Project Structure

The main development work is concentrated in two key files:

### app/main.cpp

This file contains the core audio output functionality, specifically the `AudioOutput Process()` function. This function is called whenever the audio output buffer needs to be refilled with new samples. Key responsibilities include:

- Invoking the SynthEngine's `Process()` function to generate new audio samples
- Filling the audio output buffer with these samples
- Handling button state changes and knob value updates
- Passing control values to the synth engine

### app/engine/synth_engine.h

This file contains the `SynthEngine` class, which serves as the main audio generation framework. Currently, it provides a basic structure that outputs silence (buffer filled with zeros). The class will be expanded to include:

- Full synthesis functionality
- Audio processing capabilities
- Integration with other audio components

## Development Focus

### WaveformGenerator Class

A key component to be implemented is the `WaveformGenerator` class. This class will:

- Generate various waveforms for use in the synth engine
- Accept normalized input values (0.0 - 1.0) from the waveform knob
- Modify the waveform characteristics based on the input value
- Follow the established audio processing class pattern

### Audio Processing Architecture

The project follows a consistent pattern for audio processing classes, as seen in:
- app/engine/compressor.h
- app/engine/delay_engine.h
- app/engine/biquad.h
- See app/engine/[any class that ends in .h] for other examples of audio processing classes.

Each audio processing class implements a `Process()` function that operates on individual samples. The `WaveformGenerator` will follow this same pattern, generating one sample of the waveform per `Process()` call.

## Implementation Notes

When implementing the `WaveformGenerator` class:
1. Create the new file in the app/engine directory
2. Follow the established .h file format of existing audio processors
3. Implement sample-by-sample processing
4. Ensure proper handling of the waveform control input (0.0 - 1.0)

The `WaveformGenerator` will be integrated into the `SynthEngine` class to provide the core sound generation capabilities of the synthesizer.