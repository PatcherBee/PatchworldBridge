/*
  ==============================================================================
    Source/Engine/LfoGenerator.h
    Role: Simple LFO for internal modulation
  ==============================================================================
*/
#pragma once
#include <cmath>
#include <juce_core/juce_core.h>


class LfoGenerator {
public:
  enum Waveform { Sine, Triangle, Saw, Square, Random, S_H };

  LfoGenerator() = default;

  void setSampleRate(double rate) { sampleRate = rate; }

  void setFrequency(float freqHz) {
    frequency = freqHz;
    increment =
        (frequency * juce::MathConstants<float>::twoPi) / (float)sampleRate;
  }

  void setWaveform(Waveform w) { waveform = w; }
  void setDepth(float d) { depth = d; } // 0.0 to 1.0

  /** ADSR envelope (0..1 each) applied per LFO cycle. Shapes amplitude over phase. */
  void setEnvelope(float attack, float decay, float sustain, float release) {
    envAttack = juce::jlimit(0.0f, 1.0f, attack);
    envDecay = juce::jlimit(0.0f, 1.0f, decay);
    envSustain = juce::jlimit(0.0f, 1.0f, sustain);
    envRelease = juce::jlimit(0.0f, 1.0f, release);
  }

  // Process block and return the last value (cheap approximation for control
  // rate) Or return a buffer. For CC generation, control rate (once per block)
  // is usually fine.
  float process() {
    phase += increment * 32.0f; // Advance by roughly buffer size relative if
                                // calling once per block?
    // Actually, better to advance by real time:
    // If we call this once per BLOCK (e.g. 256 samples), we need to advance
    // phase by (256 * inc). But for smoother CC, we might want sub-block. Let's
    // assume the caller manages the advancement or this is per-sample. Let's
    // make this per-sample and caller loops, OR per-block with explicit
    // 'numSamples'.

    // Simpler: Just render current phase
    float out = 0.0f;

    if (phase > juce::MathConstants<float>::twoPi)
      phase -= juce::MathConstants<float>::twoPi;

    switch (waveform) {
    case Sine:
      out = (std::sin(phase) + 1.0f) * 0.5f;
      break; // 0..1
    case Triangle:
      out = std::abs(phase / juce::MathConstants<float>::pi - 1.0f);
      break; // 0..1 approx
    case Saw:
      out = phase / juce::MathConstants<float>::twoPi;
      break; // 0..1
    case Square:
      out = (phase < juce::MathConstants<float>::pi) ? 1.0f : 0.0f;
      break;
    default:
      out = 0.0f;
      break;
    }

    if (std::abs(out) < 1.0e-8f)
      out = 0.0f;
    float env = getEnvelopeAtPhase(phase / juce::MathConstants<float>::twoPi);
    return out * depth * env;
  }

  // Proper advance method
  void advance(int numSamples) {
    if (sampleRate > 0)
      phase += (frequency * juce::MathConstants<float>::twoPi * numSamples) /
               (float)sampleRate;

    while (phase >= juce::MathConstants<float>::twoPi)
      phase -= juce::MathConstants<float>::twoPi;
  }

  /** Phase in 0..1 for UI (e.g. position bar). Safe to call from audio thread. */
  float getPhaseNormalized() const {
    float p = phase;
    const float twoPi = juce::MathConstants<float>::twoPi;
    while (p >= twoPi) p -= twoPi;
    while (p < 0.0f) p += twoPi;
    return p / twoPi;
  }

  float getCurrentValue() const {
    float p = phase;
    float out = 0.0f;
    switch (waveform) {
    case Sine:
      out = (std::sin(p) + 1.0f) * 0.5f;
      break;
    case Triangle: {
      // Triangle 0..1
      float t = p / juce::MathConstants<float>::twoPi;
      out = 2.0f * std::abs(2.0f * t - 1.0f) - 1.0f; // -1..1
      out = (out + 1.0f) * 0.5f;                     // 0..1
      break;
    }
    case Saw:
      out = p / juce::MathConstants<float>::twoPi;
      break;
    case Square:
      out = (p < juce::MathConstants<float>::pi) ? 1.0f : 0.0f;
      break;
    case Random:
      out = (float)rand() / RAND_MAX;
      break;
    case S_H:
      out = currentSH;
      break; // Sample & Hold needs trigger logic
    }
    float phase01 = phase / juce::MathConstants<float>::twoPi;
    while (phase01 >= 1.0f) phase01 -= 1.0f;
    while (phase01 < 0.0f) phase01 += 1.0f;
    float env = getEnvelopeAtPhase(phase01);
    return out * depth * env;
  }

private:
  /** Envelope value at phase 0..1 over one cycle: Attack (0->1), Decay (1->sustain), hold, Release (sustain->0). */
  float getEnvelopeAtPhase(float phase01) const {
    float a = envAttack;
    float d = envDecay;
    float r = envRelease;
    float s = envSustain;
    if (a + d + r < 0.0001f) return 1.0f; // No envelope
    float total = a + d + r;
    if (total > 1.0f) {
      float scale = 1.0f / total;
      a *= scale; d *= scale; r *= scale;
    }
    float holdStart = a + d;
    float releaseStart = 1.0f - r;
    if (phase01 <= a) {
      return (a > 0.0f) ? (phase01 / a) : 1.0f;
    }
    if (phase01 <= holdStart) {
      return (d > 0.0f) ? 1.0f + (s - 1.0f) * (phase01 - a) / d : s;
    }
    if (phase01 <= releaseStart) {
      return s;
    }
    return (r > 0.0f) ? s * (1.0f - (phase01 - releaseStart) / r) : 0.0f;
  }

  double sampleRate = 48000.0;
  float frequency = 1.0f;
  float increment = 0.0f;
  float phase = 0.0f;
  float depth = 1.0f;
  Waveform waveform = Sine;
  float currentSH = 0.0f;
  float envAttack = 0.0f;
  float envDecay = 0.3f;
  float envSustain = 1.0f;
  float envRelease = 0.3f;
};
