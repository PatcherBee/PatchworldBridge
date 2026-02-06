/*
  ==============================================================================
    Source/Core/ShortcutManager.h
    Role: Centralized keyboard shortcut management (roadmap 14.2)
  ==============================================================================
*/
#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

class ShortcutManager {
public:
  static ShortcutManager &instance() {
    static ShortcutManager mgr;
    return mgr;
  }

  void registerAction(const juce::String &actionId, const juce::KeyPress &defaultKey,
                      std::function<void()> action, const juce::String &description = "") {
    shortcuts[actionId] = {defaultKey, std::move(action), description};
  }

  void setShortcut(const juce::String &actionId, const juce::KeyPress &key) {
    if (shortcuts.count(actionId))
      shortcuts[actionId].key = key;
  }

  /** Set or replace the action callback for an existing registered action. */
  void setAction(const juce::String &actionId, std::function<void()> action) {
    auto it = shortcuts.find(actionId);
    if (it != shortcuts.end())
      it->second.action = std::move(action);
  }

  juce::KeyPress getShortcut(const juce::String &actionId) const {
    auto it = shortcuts.find(actionId);
    return it != shortcuts.end() ? it->second.key : juce::KeyPress();
  }

  bool handleKeyPress(const juce::KeyPress &key) {
    for (auto &[id, binding] : shortcuts) {
      if (key == binding.key && binding.action) {
        binding.action();
        return true;
      }
    }
    return false;
  }

  /** Get all registered actions (for UI display). */
  std::vector<std::pair<juce::String, juce::String>> getAllActions() const {
    std::vector<std::pair<juce::String, juce::String>> result;
    for (const auto &[id, binding] : shortcuts) {
      result.push_back({id, binding.description});
    }
    return result;
  }

  /** Initialize default shortcuts. */
  void initDefaults() {
    // Transport
    registerAction("transport.play", juce::KeyPress(juce::KeyPress::spaceKey), nullptr, "Play/Pause");
    registerAction("transport.stop", juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys::shiftModifier, 0), nullptr, "Stop");

    // Edit
    registerAction("edit.undo", juce::KeyPress('z', juce::ModifierKeys::ctrlModifier, 0), nullptr, "Undo");
    registerAction("edit.redo", juce::KeyPress('y', juce::ModifierKeys::ctrlModifier, 0), nullptr, "Redo");
    registerAction("edit.delete", juce::KeyPress(juce::KeyPress::deleteKey), nullptr, "Delete");
    registerAction("edit.selectAll", juce::KeyPress('a', juce::ModifierKeys::ctrlModifier, 0), nullptr, "Select All");
    registerAction("edit.copy", juce::KeyPress('c', juce::ModifierKeys::ctrlModifier, 0), nullptr, "Copy");
    registerAction("edit.paste", juce::KeyPress('v', juce::ModifierKeys::ctrlModifier, 0), nullptr, "Paste");
    registerAction("edit.duplicate", juce::KeyPress('d', juce::ModifierKeys::ctrlModifier, 0), nullptr, "Duplicate");

    // Notes
    registerAction("note.quantize", juce::KeyPress('q'), nullptr, "Quantize Selected");
    registerAction("note.merge", juce::KeyPress('g'), nullptr, "Merge Selected");
    registerAction("note.velocityUp", juce::KeyPress('+', juce::ModifierKeys::shiftModifier, 0), nullptr, "Velocity +10");
    registerAction("note.velocityDown", juce::KeyPress('-', juce::ModifierKeys::shiftModifier, 0), nullptr, "Velocity -10");
    registerAction("note.transposeUp", juce::KeyPress(juce::KeyPress::upKey), nullptr, "Transpose +1");
    registerAction("note.transposeDown", juce::KeyPress(juce::KeyPress::downKey), nullptr, "Transpose -1");
    registerAction("note.octaveUp", juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0), nullptr, "Transpose +12");
    registerAction("note.octaveDown", juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0), nullptr, "Transpose -12");

    // View
    registerAction("view.zoomIn", juce::KeyPress('=', juce::ModifierKeys::ctrlModifier, 0), nullptr, "Zoom In");
    registerAction("view.zoomOut", juce::KeyPress('-', juce::ModifierKeys::ctrlModifier, 0), nullptr, "Zoom Out");
    registerAction("view.showAllModules",
                   juce::KeyPress('s', juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier, 0),
                   nullptr, "Show all modules");
    registerAction("view.hideAllModules",
                   juce::KeyPress('h', juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier, 0),
                   nullptr, "Hide all modules");
    registerAction("view.shortcuts", juce::KeyPress(0x70, juce::ModifierKeys::noModifiers, 0),
                   nullptr, "Keyboard shortcuts");  // F1
  }

private:
  ShortcutManager() { initDefaults(); }

  struct Binding {
    juce::KeyPress key;
    std::function<void()> action;
    juce::String description;
  };
  std::map<juce::String, Binding> shortcuts;
};
