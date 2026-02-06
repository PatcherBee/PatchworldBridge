#pragma once
#include "../UI/Theme.h"
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include <vector>

class DynamicBackground : public juce::Component {
public:
  struct Particle {
    juce::Point<float> pos;
    juce::Point<float> vel;
    float size;
    float alpha;
    juce::Colour color;
  };

  DynamicBackground() {
    for (int i = 0; i < 50; ++i) {
      spawnParticle(true);
    }
  }

  void spawnParticle(bool randomPos = false) {
    Particle p;
    if (randomPos) {
      p.pos = {(float)juce::Random::getSystemRandom().nextInt(1920),
               (float)juce::Random::getSystemRandom().nextInt(1080)};
    } else {
      p.pos = {(float)getWidth() / 2.0f, (float)getHeight() / 2.0f};
    }

    p.vel = {(juce::Random::getSystemRandom().nextFloat() - 0.5f) * 2.0f,
             (juce::Random::getSystemRandom().nextFloat() - 0.5f) * 2.0f};
    p.size = 2.0f + juce::Random::getSystemRandom().nextFloat() * 4.0f;
    p.alpha = 0.1f + juce::Random::getSystemRandom().nextFloat() * 0.4f;
    p.color = Theme::accent;
    particles.push_back(p);
  }

  void onActivity() {
    // Pulse background or spawn more particles on MIDI activity
    // Thread-safe trigger (Relaxed is fine for simple signaling)
    triggerSpawn.store(true, std::memory_order_relaxed);
  }

  /** True only for animated themes (10–13); used for repaint throttling. */
  bool hasActiveParticles() const {
    return ThemeManager::isAnimatedTheme(Theme::currentThemeId) &&
           (!particles.empty() || intensity > 0.01f);
  }

  void paint(juce::Graphics &g) override {
    // Background is handled by CRT/clear in MainComponent (OpenGL). Grid/particles only for animated themes.
    if (!ThemeManager::isAnimatedTheme(Theme::currentThemeId))
      return;
    g.setColour(Theme::accent.withAlpha(0.05f + (intensity * 0.1f)));
    float gridSize = 40.0f;
    for (float x = 0; x < getWidth(); x += gridSize)
      g.drawVerticalLine((int)x, 0, (float)getHeight());
    for (float y = 0; y < getHeight(); y += gridSize)
      g.drawHorizontalLine((int)y, 0, (float)getWidth());

    for (auto &p : particles) {
      g.setColour(p.color.withAlpha(p.alpha * (1.0f + intensity)));
      g.fillRect(p.pos.x, p.pos.y, p.size, p.size);
      if (intensity > 0.1f) {
        g.setColour(p.color.withAlpha(p.alpha * intensity * 0.5f));
        g.fillRect(p.pos.x - p.size, p.pos.y - p.size, p.size * 3, p.size * 3);
      }
    }
  }

  void updateAnimation() { updateAnimation(0.016f); }

  void updateAnimation(float dt) {
    if (!isVisible() || !ThemeManager::isAnimatedTheme(Theme::currentThemeId))
      return;

    if (triggerSpawn.exchange(false, std::memory_order_relaxed)) {
      intensity = 1.0f;
      if (particles.size() < 150)
        spawnParticle();
    }
    int w = getWidth();
    int h = getHeight();
    if (w <= 0 || h <= 0)
      return;

    float decay = 1.0f - 3.0f * dt;
    if (decay < 0.0f)
      decay = 0.0f;
    for (auto it = particles.begin(); it != particles.end();) {
      it->pos += it->vel * dt * 60.0f;
      it->alpha -= 0.06f * dt;

      if (it->alpha <= 0 || it->pos.x < 0 || it->pos.x > (float)w ||
          it->pos.y < 0 || it->pos.y > (float)h) {
        it = particles.erase(it);
      } else {
        ++it;
      }
    }

    if (particles.size() < 40)
      spawnParticle(true);

    if (intensity > 0.01f)
      intensity *= (0.95f * decay + (1.0f - decay));
    else
      intensity = 0.0f;

    static float animStep = 0.0f;
    if (auto *lf = &getLookAndFeel())
      ThemeManager::updateAnimation(Theme::currentThemeId, animStep, *lf);

    repaint();
  }

  void resized() override {
    // Reset or adjust particles on resize if needed
  }

  void setPhase(float ph) {
    phaseOffset = ph;
    intensity = 0.5f; // Slight pulse on phase change driving
    repaint();
  }

private:
  float phaseOffset = 0.0f;
  std::atomic<bool> triggerSpawn{false};
  std::vector<Particle> particles;
  float intensity = 0.0f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicBackground)
};
