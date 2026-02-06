/*
  ==============================================================================
    Source/UI/NoteEditUndoAction.h
    Role: Undoable actions for SpliceEditor note edits
  ==============================================================================
*/
#pragma once

#include "../Audio/EditableNote.h"
#include "../Core/BridgeContext.h"
#include "Panels/SpliceEditor.h"
#include <juce_data_structures/juce_data_structures.h>
#include <set>
#include <vector>

/** Undoable action that stores before/after note state. */
class NoteEditUndoAction : public juce::UndoableAction {
public:
  NoteEditUndoAction(SpliceEditor *editor, BridgeContext *ctx,
                     std::vector<EditableNote> beforeNotes,
                     std::set<int> beforeSelected,
                     std::vector<EditableNote> afterNotes,
                     std::set<int> afterSelected,
                     const juce::String &description)
      : editor(editor), context(ctx),
        beforeNotes(std::move(beforeNotes)), beforeSelected(std::move(beforeSelected)),
        afterNotes(std::move(afterNotes)), afterSelected(std::move(afterSelected)),
        desc(description) {}

  bool perform() override {
    return applyState(afterNotes, afterSelected);
  }

  bool undo() override {
    return applyState(beforeNotes, beforeSelected);
  }

  int getSizeInUnits() override {
    return (int)((beforeNotes.size() + afterNotes.size()) * sizeof(EditableNote) +
                 (beforeSelected.size() + afterSelected.size()) * sizeof(int));
  }

  juce::String getDescription() const { return desc; }

private:
  SpliceEditor *editor = nullptr;
  BridgeContext *context = nullptr;
  std::vector<EditableNote> beforeNotes, afterNotes;
  std::set<int> beforeSelected, afterSelected;
  juce::String desc;

  bool applyState(const std::vector<EditableNote> &notes,
                  const std::set<int> &selected) {
    if (!editor)
      return false;
    editor->applyState(notes, selected);
    return true;
  }
};
