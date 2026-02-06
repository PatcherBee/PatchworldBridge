/*
  ==============================================================================
    Source/UI/MainComponentEvents.cpp
    Role: Key, mouse, and file-drop event handlers (split from MainComponent).
  ==============================================================================
*/
#include "UI/MainComponent.h"
#include "UI/Widgets/ModuleWindow.h"

#include <juce_gui_basics/juce_gui_basics.h>

// --- File drag ---
bool MainComponent::isInterestedInFileDrag(const juce::StringArray &files) {
  juce::ignoreUnused(files);
  return true;
}

void MainComponent::filesDropped(const juce::StringArray &files, int x, int y) {
  juce::ignoreUnused(x, y);
  if (sysController)
    sysController->handleFileDrop(files);
}

// --- Mouse (marquee select + background-drag multiple ModuleWindows) ---
void MainComponent::mouseDown(const juce::MouseEvent &e) {
  grabKeyboardFocus();

  // When Virtual Keyboard is selected as MIDI input, clicking anywhere grabs focus for Editor keyboard
  if (getContext() && getContext()->appState.getActiveMidiIds(true).contains("VirtualKeyboard") &&
      performancePanel && winEditor && winEditor->isVisible())
    performancePanel->horizontalKeyboard.grabKeyboardFocus();

  // Right-click on empty main window spot: Add Modules context menu
  if (currentView == AppView::Dashboard && e.mods.isRightButtonDown()) {
    if (getComponentAt(e.getPosition()) == this) {
      showAddModulesContextMenu(e);
      return;
    }
  }

  if (currentView == AppView::Dashboard && !e.mods.isRightButtonDown()) {
    auto *hitComp = getComponentAt(e.getPosition());
    if (hitComp == this) {
      boxSelectStart = e.getPosition();
      lastBackgroundDragPos = e.getPosition();
      if (!e.mods.isShiftDown())
        ModuleWindow::clearSelection();
      // Don't set isBoxSelecting yet — commit on first drag (marquee vs move selection)
    }
  }
}

void MainComponent::mouseDrag(const juce::MouseEvent &e) {
  if (currentView != AppView::Dashboard) return;

  auto currentPos = e.getPosition();
  int dist = boxSelectStart.getDistanceFrom(currentPos);

  // Commit to marquee or background-drag after a small movement
  if (!isBoxSelecting && !isBackgroundDragging && dist > 4) {
    if (ModuleWindow::hasSelection()) {
      isBackgroundDragging = true;
      lastBackgroundDragPos = currentPos;
    } else {
      isBoxSelecting = true;
      selectionBox = juce::Rectangle<int>(juce::jmin(boxSelectStart.x, currentPos.x),
                                         juce::jmin(boxSelectStart.y, currentPos.y),
                                         juce::jmax(boxSelectStart.x, currentPos.x) - juce::jmin(boxSelectStart.x, currentPos.x),
                                         juce::jmax(boxSelectStart.y, currentPos.y) - juce::jmin(boxSelectStart.y, currentPos.y));
    }
  }

  if (isBoxSelecting) {
    selectionBox = juce::Rectangle<int>(juce::jmin(boxSelectStart.x, currentPos.x),
                                       juce::jmin(boxSelectStart.y, currentPos.y),
                                       juce::jmax(boxSelectStart.x, currentPos.x) - juce::jmin(boxSelectStart.x, currentPos.x),
                                       juce::jmax(boxSelectStart.y, currentPos.y) - juce::jmin(boxSelectStart.y, currentPos.y));
    for (auto *w : {winEditor.get(), winMixer.get(), winSequencer.get(),
                    winPlaylist.get(), winLog.get(), winArp.get(),
                    winMacros.get(), winChords.get(), winLfoGen.get(),
                    winControl.get()}) {
      if (w && w->isVisible() && selectionBox.intersects(w->getBounds()))
        w->repaint();
    }
    repaint();
  } else if (isBackgroundDragging) {
    int dx = currentPos.x - lastBackgroundDragPos.x;
    int dy = currentPos.y - lastBackgroundDragPos.y;
    lastBackgroundDragPos = currentPos;
    ModuleWindow::moveSelectedWindows(this, dx, dy);
  }
}

void MainComponent::mouseUp(const juce::MouseEvent &) {
  if (isBoxSelecting) {
    int rx = juce::jmin(selectionBox.getX(), selectionBox.getRight());
    int ry = juce::jmin(selectionBox.getY(), selectionBox.getBottom());
    int rw = juce::jmax(selectionBox.getX(), selectionBox.getRight()) - rx;
    int rh = juce::jmax(selectionBox.getY(), selectionBox.getBottom()) - ry;
    juce::Rectangle<int> r(rx, ry, rw, rh);
    if (r.getWidth() >= 2 && r.getHeight() >= 2)
      ModuleWindow::selectWindowsInRect(this, r);
    for (auto *w : {winEditor.get(), winMixer.get(), winSequencer.get(),
                    winPlaylist.get(), winLog.get(), winArp.get(),
                    winMacros.get(), winChords.get(), winLfoGen.get(),
                    winControl.get()}) {
      if (w && w->isVisible()) w->repaint();
    }
  }
  isBoxSelecting = false;
  isBackgroundDragging = false;
  repaint();
}

void MainComponent::showAddModulesContextMenu(const juce::MouseEvent &) {
  juce::PopupMenu addModulesSub;
  auto addModuleItem = [&addModulesSub](const juce::String &name, ModuleWindow *win) {
    if (!win) return;
    addModulesSub.addItem(name, true, win->isVisible(), [win] {
      bool willShow = !win->isVisible();
      if (willShow) {
        win->setVisible(true);
        win->toFront(true);
      } else {
        win->setVisible(false);
      }
    });
  };
  addModuleItem("Editor", winEditor.get());
  addModuleItem("Mixer", winMixer.get());
  addModuleItem("Sequencer", winSequencer.get());
  addModuleItem("Playlist", winPlaylist.get());
  addModuleItem("Arpeggiator", winArp.get());
  addModuleItem("Macros", winMacros.get());
  addModuleItem("Log", winLog.get());
  addModuleItem("Chords", winChords.get());
  addModuleItem("Control", winControl.get());
  addModuleItem("LFO Generator", winLfoGen.get());

  juce::PopupMenu m;
  m.addSubMenu("Add Modules", addModulesSub, true);

  m.showMenuAsync(juce::PopupMenu::Options()
                      .withTargetComponent(this)
                      .withParentComponent(nullptr)
                      .withMinimumWidth(160)
                      .withStandardItemHeight(24),
                  nullptr);
}

// --- Keyboard ---
bool MainComponent::keyPressed(const juce::KeyPress &key) {
  if (sysController && sysController->handleGlobalKeyPress(key))
    return true;
  return false;
}
