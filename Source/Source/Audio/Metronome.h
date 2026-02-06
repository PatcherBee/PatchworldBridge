/*
  ==============================================================================
    Source/Audio/Metronome.h
    Role: Built-in click track for tempo reference.
  ==============================================================================
*/
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

class Metronome {
public:
  Metronome() = default;

  void prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    generateClickSounds();
  }

  void setEnabled(bool e) { enabled_ = e; }
  bool isEnabled() const { return enabled_; }

  /** Click volume 0–1. */
  void setVolume(float v) { volume_ = juce::jlimit(0.0f, 1.0f, v); }
  float getVolume() const { return volume_; }

  /** Click sound: 0=Sine, 1=Tick (short), 2=Beep (square). */
  enum ClickType { Sine = 0, Tick, Beep };
  void setClickType(ClickType t) { clickType_ = t; generateClickSounds(); }
  ClickType getClickType() const { return clickType_; }

  void processBlock(juce::AudioBuffer<float> &buffer, int startSample,
                    int numSamples, double currentBeat, double bpm) {
    if (!enabled_ || numSamples <= 0)
      return;
    double beatsPerSample = bpm / 60.0 / sampleRate_;

    for (int i = 0; i < numSamples; ++i) {
      int bufIdx = startSample + i;
      double beat = currentBeat + (double)i * beatsPerSample;
      int beatInt = (int)beat;
      double beatFrac = beat - beatInt;

      if (beatFrac < beatsPerSample && beatFrac >= 0.0) {
        bool isDownbeat = (beatInt % 4 == 0);
        auto &click = isDownbeat ? clickHigh_ : clickLow_;

        float vol = volume_;
        for (int ch = 0; ch < buffer.getNumChannels() && ch < 2; ++ch) {
          for (int j = 0; j < click.getNumSamples() &&
                         (bufIdx + j) < buffer.getNumSamples();
               ++j) {
            buffer.addSample(ch, bufIdx + j, click.getSample(0, j) * vol);
          }
        }
      }
    }
  }

private:
  float sampleRate_ = 44100.0f;
  bool enabled_ = false;
  float volume_ = 0.7f;
  ClickType clickType_ = Sine;
  juce::AudioBuffer<float> clickHigh_, clickLow_;

  void generateClickSounds() {
    int clickLength = (int)(sampleRate_ * (clickType_ == Tick ? 0.004f : 0.012f));
    if (clickLength < 1)
      clickLength = 1;
    clickHigh_.setSize(1, clickLength);
    clickLow_.setSize(1, clickLength);

    const float pi = juce::MathConstants<float>::pi;
    const float gainHigh = 0.22f;
    const float gainLow = 0.16f;

    for (int i = 0; i < clickLength; ++i) {
      float t = (float)i / clickLength;
      float env = 1.0f - t;
      env = env * env;

      float sampleHigh, sampleLow;
      if (clickType_ == Beep) {
        // Square wave beep
        sampleHigh = (std::sin(i * 2.0f * pi * 900.0f / sampleRate_) >= 0 ? 1.0f : -1.0f) * env * gainHigh * 0.6f;
        sampleLow = (std::sin(i * 2.0f * pi * 700.0f / sampleRate_) >= 0 ? 1.0f : -1.0f) * env * gainLow * 0.6f;
      } else if (clickType_ == Tick) {
        sampleHigh = std::sin(i * 2.0f * pi * 1200.0f / sampleRate_) * env * gainHigh * 1.2f;
        sampleLow = std::sin(i * 2.0f * pi * 900.0f / sampleRate_) * env * gainLow * 1.2f;
      } else {
        sampleHigh = std::sin(i * 2.0f * pi * 900.0f / sampleRate_) * env * gainHigh;
        sampleLow = std::sin(i * 2.0f * pi * 700.0f / sampleRate_) * env * gainLow;
      }
      clickHigh_.setSample(0, i, sampleHigh);
      clickLow_.setSample(0, i, sampleLow);
    }
  }
};
