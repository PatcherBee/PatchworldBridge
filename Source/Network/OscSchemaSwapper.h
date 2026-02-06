/*
  ==============================================================================

    OscSchemaSwapper.h
    Created: 25 Jan 2026

    Manages atomic schema generations to handle "Hot Swap" of OSC addresses
    without stuck notes. Keeps old schemas alive in a tail buffer until
    pending notes are cleared or timeout functionality allows.

  ==============================================================================
*/

#pragma once

#include "../Audio/OscTypes.h"
#include "../Services/DeferredDeleter.h"
#include <atomic>
#include <juce_core/juce_core.h>
#include <memory>


struct SchemaSlot {
  std::shared_ptr<OscNamingSchema> schema;
  uint64_t generation = 0;
};

class OscSchemaSwapper {
public:
  OscSchemaSwapper() {
    // Initialize with default schema
    auto initialSlot = std::make_shared<SchemaSlot>(
        SchemaSlot{std::make_shared<OscNamingSchema>(), 1});
    std::atomic_store(&currentSlot, initialSlot);
  }

  ~OscSchemaSwapper() = default;

  // Set the Deleter (Deprecated - atomic shared_ptr handles cleanup)
  void setDeleter(DeferredDeleter *d) { juce::ignoreUnused(d); }

  // Called by UI thread when addresses change
  void updateSchema(std::shared_ptr<OscNamingSchema> newSchema) {
    auto old = std::atomic_load(&currentSlot);
    uint64_t nextGen = old ? old->generation + 1 : 1;

    // Create new slot
    auto newSlot = std::make_shared<SchemaSlot>(SchemaSlot{newSchema, nextGen});

    // Atomic swap
    auto prevTail = std::atomic_exchange(&currentSlot, newSlot);
    std::atomic_store(&tailSlot, prevTail);
  }

  // Called on Audio/Network Thread
  std::pair<std::shared_ptr<OscNamingSchema>, uint64_t> getSchemaForNoteOn() {
    auto slot = std::atomic_load(&currentSlot);
    if (!slot)
      return {nullptr, 0};
    return {slot->schema, slot->generation};
  }

  // Called on Audio/Network Thread
  std::shared_ptr<OscNamingSchema> getSchemaForGeneration(uint64_t gen) {
    auto current = std::atomic_load(&currentSlot);
    if (current && current->generation == gen)
      return current->schema;

    auto tail = std::atomic_load(&tailSlot);
    if (tail && tail->generation == gen)
      return tail->schema;

    return current ? current->schema : nullptr;
  }

private:
  std::shared_ptr<SchemaSlot> currentSlot;
  std::shared_ptr<SchemaSlot> tailSlot;
};
