#pragma once
#include <juce_core/juce_core.h>
#include <juce_osc/juce_osc.h>
#include <memory>
#include <unordered_map>
#include <vector>

// --- Project Headers ---
#include "../Audio/OscTypes.h"

struct OscLookup {
  using RouteMap = std::unordered_map<uint64_t, OscRoute>;

  static std::shared_ptr<RouteMap>
  createMapFromSchema(const OscNamingSchema &schema) {
    auto newMap = std::make_shared<RouteMap>();
    newMap->reserve(16 * 10 + 10);

    for (int ch = 1; ch <= 16; ++ch) {
      juce::String chStr = juce::String(ch);

      // Standard Addresses (from schema)
      (*newMap)[oscHash(
          schema.getAddress(schema.notePrefix, ch, schema.noteSuffix)
              .toRawUTF8())] = OscRoute(BridgeEvent::NoteOn, ch);
      (*newMap)[oscHash(
          schema.getAddress(schema.notePrefix, ch, schema.noteOffSuffix)
              .toRawUTF8())] = OscRoute(BridgeEvent::NoteOff, ch);
      (*newMap)[oscHash(schema.getAddress(schema.ccPrefix, ch, schema.ccSuffix)
                            .toRawUTF8())] = OscRoute(BridgeEvent::CC, ch);
      (*newMap)[oscHash(
          schema.getAddress(schema.pitchPrefix, ch, schema.pitchSuffix)
              .toRawUTF8())] = OscRoute(BridgeEvent::Pitch, ch);

      // --- User Alternate Receive Addresses (/chXn, /chXnv, etc.) ---
      (*newMap)[oscHash(juce::String("/ch" + chStr + "n").toRawUTF8())] =
          OscRoute(BridgeEvent::NoteOn, ch);
      (*newMap)[oscHash(juce::String("/ch" + chStr + "nv").toRawUTF8())] =
          OscRoute(BridgeEvent::NoteOn, ch);
      (*newMap)[oscHash(juce::String("/ch" + chStr + "noff").toRawUTF8())] =
          OscRoute(BridgeEvent::NoteOff, ch);
      (*newMap)[oscHash(juce::String("/ch" + chStr + "c").toRawUTF8())] =
          OscRoute(BridgeEvent::CC, ch);
      (*newMap)[oscHash(juce::String("/ch" + chStr + "wheel").toRawUTF8())] =
          OscRoute(BridgeEvent::Pitch, ch);
      (*newMap)[oscHash(juce::String("/ch" + chStr + "press").toRawUTF8())] =
          OscRoute(BridgeEvent::Aftertouch, ch);
      (*newMap)[oscHash(juce::String("/ch" + chStr + "s").toRawUTF8())] =
          OscRoute(BridgeEvent::CC, ch, 64); // Sustain
    }

    // Transport
    (*newMap)[oscHash(schema.playAddr.toRawUTF8())] =
        OscRoute(BridgeEvent::Transport, 0, 0, 1.0f);
    (*newMap)[oscHash(schema.stopAddr.toRawUTF8())] =
        OscRoute(BridgeEvent::Transport, 0, 0, 0.0f);

    // System
    (*newMap)[oscHash("/panic")] = OscRoute(BridgeEvent::Panic, 0);

    // Playlist
    (*newMap)[oscHash("/playlist/next")] =
        OscRoute(BridgeEvent::PlaylistCommand, 0, 1);
    (*newMap)[oscHash("/playlist/prev")] =
        OscRoute(BridgeEvent::PlaylistCommand, 0, -1);
    (*newMap)[oscHash("/playlist/select")] =
        OscRoute(BridgeEvent::PlaylistCommand, 0, 0);

    return newMap;
  }
};
