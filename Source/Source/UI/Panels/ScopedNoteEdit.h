/*
  ==============================================================================
    Source/UI/Panels/ScopedNoteEdit.h
    Role: RAII wrapper for SpliceEditor beginEdit/endEdit.
  ==============================================================================
*/
#pragma once

#include "SpliceEditor.h"

struct ScopedNoteEdit {
  explicit ScopedNoteEdit(SpliceEditor& ed) : editor(&ed) {
    editor->beginEdit();
  }
  ~ScopedNoteEdit() {
    if (editor)
      editor->endEdit();
  }

  ScopedNoteEdit(const ScopedNoteEdit&) = delete;
  ScopedNoteEdit& operator=(const ScopedNoteEdit&) = delete;
  ScopedNoteEdit(ScopedNoteEdit&& other) noexcept : editor(other.editor) {
    other.editor = nullptr;
  }
  ScopedNoteEdit& operator=(ScopedNoteEdit&& other) noexcept {
    if (this != &other) {
      if (editor) editor->endEdit();
      editor = other.editor;
      other.editor = nullptr;
    }
    return *this;
  }

private:
  SpliceEditor* editor = nullptr;
};
