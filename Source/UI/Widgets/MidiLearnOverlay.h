/*
  ==============================================================================
    Source/Components/MidiLearnOverlay.h
    Status: CRASH FIXED (Removed infinite recursion in hitTest)
  ==============================================================================
*/
#pragma once
#include "../Animation.h"
#include "../PopupMenuOptions.h"
#include "Services/MidiMappingService.h"
#include "../Theme.h"
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Helper model for the mapping list
class MappingListModel : public juce::ListBoxModel {
public:
  MidiMappingService &manager;
  std::function<void(int)> onRightClick;

  MappingListModel(MidiMappingService &m) : manager(m) {}

  int getNumRows() override {
    const juce::ScopedReadLock sl(manager.mappingLock);
    return (int)manager.mappings.size();
  }

  void paintListBoxItem(int row, juce::Graphics &g, int w, int h,
                        bool selected) override {
    const juce::ScopedReadLock sl(manager.mappingLock);
    if (row >= manager.mappings.size())
      return;
    auto &m = manager.mappings[row];

    auto r = juce::Rectangle<int>(0, 0, w, h).toFloat().reduced(4);

    // 1. Draw Background
    g.setColour(selected ? Theme::accent.withAlpha(0.2f)
                         : juce::Colours::black.withAlpha(0.2f));
    g.fillRoundedRectangle(r, 4.0f);

    // 2. Draw Controller Info (Left)
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
    g.drawText(m.controllerName + " (Ch " + juce::String(m.source.channel) +
                   ")",
               10, 0, w / 3, h, juce::Justification::centredLeft);

    // 3. Draw Parameter Target (Middle)
    g.setColour(Theme::accent);
    g.drawText("➔  " + m.target.paramID, w / 3, 0, (int)(w * 0.4f), h,
               juce::Justification::centredLeft);

    // 4. Draw Curve Visualizer (Right)
    auto curveRect =
        juce::Rectangle<float>((float)w - 110, 8.0f, 40.0f, (float)h - 16.0f);
    drawCurveThumbnail(g, curveRect, (MappingEntry::Curve)m.curve, m.inverted);

    if (m.inverted) {
      g.setColour(juce::Colours::orange);
      g.setFont(juce::FontOptions(10.0f));
      g.drawText("INV", w - 45, 0, 30, h, juce::Justification::centred);
    }
  }

  void drawCurveThumbnail(juce::Graphics &g, juce::Rectangle<float> r,
                          MappingEntry::Curve curve, bool inv) {
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRect(r, 0.5f);

    juce::Path p;
    for (float i = 0; i <= 1.0f; i += 0.1f) {
      float x = r.getX() + (i * r.getWidth());
      float val = MidiMappingService::applyCurve(inv ? (1.0f - i) : i, curve);
      float y = r.getBottom() - (val * r.getHeight());

      if (i == 0.0f)
        p.startNewSubPath(x, y);
      else
        p.lineTo(x, y);
    }
    g.setColour(Theme::accent.withAlpha(0.8f));
    g.strokePath(p, juce::PathStrokeType(1.2f));
  }

  void listBoxItemClicked(int row, const juce::MouseEvent &e) override {
    if (e.mods.isRightButtonDown() && onRightClick) {
      onRightClick(row);
    }
  }
};

class MidiLearnOverlay : public juce::Component {
public:
  std::function<void()> onSearchRequested;
  std::function<void()> onDone;  // Exit learn mode (e.g. from Done button)

  MidiLearnOverlay(MidiMappingService &m, juce::Component &root)
      : manager(m), rootContent(root), listModel(m) {
    // 1. MAPPING LIST
    mapList.setModel(&listModel);
    mapList.setRowHeight(30);
    mapList.setColour(juce::ListBox::backgroundColourId,
                      juce::Colours::black.withAlpha(0.8f));
    addAndMakeVisible(mapList);

    // 2. CLEAR ALL BUTTON
    btnClearAll.setButtonText("Clear All Mappings");
    btnClearAll.setColour(juce::TextButton::buttonColourId,
                          juce::Colours::red.darker(0.5f));
    btnClearAll.onClick = [this] {
      juce::Component::SafePointer<MidiLearnOverlay> safe(this);
      juce::NativeMessageBox::showOkCancelBox(
          juce::MessageBoxIconType::WarningIcon, "Clear all mappings",
          "Remove all MIDI learn mappings? This cannot be undone.",
          getTopLevelComponent(),
          juce::ModalCallbackFunction::create([safe](int result) {
            if (result == 1 && safe != nullptr) {
              safe->manager.resetMappings();
              safe->mapList.updateContent();
              safe->repaint();
            }
          }));
    };
    btnClearAll.setInterceptsMouseClicks(true, true);
    addAndMakeVisible(btnClearAll);

    // 3. SEARCH BUTTON
    btnSearch.setButtonText("Search Params (Ctrl+P)");
    btnSearch.setColour(juce::TextButton::buttonColourId,
                        juce::Colours::steelblue.darker(0.3f));
    btnSearch.setInterceptsMouseClicks(true, true);
    btnSearch.onClick = [this] {
      if (onSearchRequested)
        onSearchRequested();
    };
    addAndMakeVisible(btnSearch);

    // 3b. DONE button - always-visible exit so user can leave learn mode
    btnDone.setButtonText("Done");
    btnDone.setColour(juce::TextButton::buttonColourId, Theme::accent.darker(0.2f));
    btnDone.setInterceptsMouseClicks(true, true);
    btnDone.onClick = [this] {
      if (onDone)
        onDone();
    };
    addAndMakeVisible(btnDone);

    // 3c. Move list button - flip Active Mappings to other side so it doesn't block controls
    btnMoveList.setButtonText("Move list \u2194");
    btnMoveList.setTooltip("Move Active Mappings list to the other side of the screen.");
    btnMoveList.onClick = [this] {
      listOnRight_ = !listOnRight_;
      resized();
      repaint();
    };
    addAndMakeVisible(btnMoveList);

    // 4. MAPPING LIST INTERCEPTS
    mapList.setInterceptsMouseClicks(true, true);

    // 5. OVERLAY INTERCEPTS (Allow catching clicks for learn logic + child
    // buttons)
    setInterceptsMouseClicks(true, true);

    setVisible(false);

    // RIGHT CLICK MENU LOGIC
    listModel.onRightClick = [this](int row) {
      auto *entry = manager.getEntryAtRow(row);
      if (entry == nullptr)
        return;

      juce::PopupMenu menu;
      menu.addSectionHeader(entry->target.paramID);

      // CURVE SUBMENU
      juce::PopupMenu curves;
      curves.addItem(juce::PopupMenu::Item("Linear")
                         .setTicked(entry->curve == MappingEntry::Linear)
                         .setAction([this, entry] {
                           entry->curve = MappingEntry::Linear;
                         }));
      curves.addItem(
          juce::PopupMenu::Item("Logarithmic")
              .setTicked(entry->curve == MappingEntry::Log)
              .setAction([this, entry] { entry->curve = MappingEntry::Log; }));
      curves.addItem(
          juce::PopupMenu::Item("Exponential")
              .setTicked(entry->curve == MappingEntry::Exp)
              .setAction([this, entry] { entry->curve = MappingEntry::Exp; }));
      curves.addItem(juce::PopupMenu::Item("S-Curve")
                         .setTicked(entry->curve == MappingEntry::S_Curve)
                         .setAction([this, entry] {
                           entry->curve = MappingEntry::S_Curve;
                         }));

      menu.addSubMenu("Response Curve", curves);

      // RANGE PRESETS
      juce::PopupMenu ranges;
      ranges.addItem("Full (0-100%)", [entry, this] {
        entry->target.minRange = 0.0f;
        entry->target.maxRange = 1.0f;
      });
      ranges.addItem("Low Half (0-50%)", [entry, this] {
        entry->target.minRange = 0.0f;
        entry->target.maxRange = 0.5f;
      });
      ranges.addItem("High Half (50-100%)", [entry, this] {
        entry->target.minRange = 0.5f;
        entry->target.maxRange = 1.0f;
      });
      menu.addSubMenu("Range Presets", ranges);

      // TOGGLES
      menu.addItem(juce::PopupMenu::Item("Invert Direction")
                       .setTicked(entry->inverted)
                       .setAction([this, entry] {
                         entry->inverted = !entry->inverted;
                         manager.isDirty.store(true);
                         manager.triggerAsyncUpdate();
                       }));

      menu.addSeparator();
      menu.addItem("Delete Mapping", [this, entry] {
        manager.removeMappingForParam(entry->target.paramID);
        mapList.updateContent();
      });

      menu.showMenuAsync(PopupMenuOptions::forComponent(&mapList));
    };
  }

  void updateHoles(juce::Rectangle<int> log, juce::Rectangle<int> btn) {
    logArea = log;
    learnBtnArea = btn;
    repaint();
  }

  void setOverlayActive(bool active) {
    if (active) {
      setVisible(true);
      Animation::fade(*this, 1.0f);
      mapList.updateContent();
      toFront(true);
      grabKeyboardFocus(); // Ensure Ctrl+P / Esc work
    } else {
      Animation::fade(*this, 0.0f);
      juce::Component::SafePointer<MidiLearnOverlay> safe(this);
      juce::Timer::callAfterDelay(Animation::defaultDurationMs + 20, [safe] {
        if (safe)
          safe->setVisible(false);
      });
    }
  }

  /** Call when mappings change (e.g. after learn) so Active Mappings list updates. */
  void refreshMappingList() {
    mapList.updateContent();
    repaint();
  }

  void resized() override {
    auto r = getLocalBounds();

    // Top row: Search + Done + Move list
    auto topRow = r.removeFromTop(40).reduced(20, 5);
    btnSearch.setBounds(topRow.removeFromLeft(180));
    btnMoveList.setBounds(topRow.removeFromRight(90).reduced(2));
    btnDone.setBounds(topRow.removeFromRight(80));

    // Mapping list pane: left or right so user can avoid blocking controls
    juce::Rectangle<int> listPane;
    if (listOnRight_) {
      listPane = r.removeFromRight(320).reduced(20);
    } else {
      listPane = r.removeFromLeft(320).reduced(20);
    }
    btnClearAll.setBounds(listPane.removeFromBottom(30));
    listPane.removeFromBottom(10);
    mapList.setBounds(listPane);
  }

  bool hitTest(int x, int y) override {
    // Holes: let clicks pass through to log area and learn button
    if (!logArea.isEmpty() && logArea.contains(x, y))
      return false;
    if (!learnBtnArea.isEmpty() && learnBtnArea.contains(x, y))
      return false;
    if (mapList.getBounds().contains(x, y))
      return true;
    if (btnSearch.getBounds().contains(x, y))
      return true;
    if (btnMoveList.getBounds().contains(x, y))
      return true;
    if (btnDone.getBounds().contains(x, y))
      return true;
    if (btnClearAll.getBounds().contains(x, y))
      return true;
    return true; // Rest: receive for hover/learn so we can highlight mappable controls
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds();

    // 1. PUNCH HOLES: Path-based approach for smooth rendering
    juce::Path bgPath;
    bgPath.addRectangle(bounds.toFloat());

    if (!logArea.isEmpty())
      bgPath.addRectangle(logArea.toFloat());
    if (!learnBtnArea.isEmpty())
      bgPath.addRectangle(learnBtnArea.toFloat());

    // --- FIX BEGIN: Use setUsingNonZeroWinding(false) for "Even-Odd" hole
    // punching ---
    bgPath.setUsingNonZeroWinding(false);

    g.setColour(juce::Colours::black.withAlpha(0.65f));
    g.fillPath(bgPath);
    // --- FIX END ---

    // Subtle glow around interactive holes
    g.setColour(Theme::accent.withAlpha(0.3f));
    if (!learnBtnArea.isEmpty())
      g.drawRoundedRectangle(learnBtnArea.toFloat().expanded(2.0f), 4.0f, 2.0f);

    // 3. Mapping Panel Title (above list; list may be left or right)
    auto listArea = mapList.getBounds();
    g.setColour(Theme::accent);
    g.setFont(juce::FontOptions(16.0f));
    g.drawText("Active Mappings", listArea.translated(0, -25).withHeight(25),
               juce::Justification::centredLeft);

    // 4. Highlight Components
    auto queue = manager.getLearnQueue();
    auto now = juce::Time::getMillisecondCounter();

    for (const auto &paramID : queue) {
      if (auto *c = findComponentWithParamID(paramID)) {
        auto b = getLocalArea(c, c->getLocalBounds());

        // SUCCESS FLASH: Check if this component just received MIDI in the last
        // 300ms
        bool justLearned = (now - manager.getLastLearnTime(paramID) < 300);

        if (justLearned) {
          g.setColour(juce::Colours::limegreen.withAlpha(0.6f));
          g.drawRect(b.toFloat().expanded(2.0f),
                     3.0f); // Thicker "success" border
        } else {
          g.setColour(juce::Colours::orange.withAlpha(0.4f));
          g.drawRect(b.toFloat(), 2.0f);
        }

        g.setColour(justLearned ? juce::Colours::limegreen.withAlpha(0.2f)
                                : juce::Colours::orange.withAlpha(0.1f));
        g.fillRect(b);
      }
    }

    // Same box on hover as when in learn queue: only show for mappable controls (hoveredComponent has paramID)
    if (hoveredComponent) {
      auto r =
          getLocalArea(hoveredComponent, hoveredComponent->getLocalBounds());
      juce::String pid = hoveredComponent->getProperties()["paramID"];
      bool mapped = manager.isParameterMapped(pid);
      if (mapped) {
        g.setColour(juce::Colours::red.withAlpha(0.5f));
        g.drawRect(r.toFloat(), 2.0f);
        g.drawText("Click to Unmap", r.translated(0, -20),
                   juce::Justification::centred);
        g.setColour(juce::Colours::yellow);
        g.drawRect(r.toFloat(), 2.0f);
      } else {
        // Unmapped mappable: same orange style as learn-queue highlight
        g.setColour(juce::Colours::orange.withAlpha(0.4f));
        g.drawRect(r.toFloat(), 2.0f);
        g.setColour(juce::Colours::orange.withAlpha(0.1f));
        g.fillRect(r);
      }
    }

    if (!queue.empty()) {
      auto r = getLocalBounds().removeFromTop(80).reduced(20).translated(0, 40);
      g.setColour(juce::Colours::black.withAlpha(0.8f));
      g.fillRect(r.toFloat());
      g.setColour(juce::Colours::yellow);
      g.drawRect(r.toFloat(), 2.0f);
      g.setFont(juce::FontOptions(20.0f));
      g.drawFittedText("LEARNING mode active...", r,
                       juce::Justification::centred, 2);
    }
  }

  void mouseDown(const juce::MouseEvent &e) override {
    // Update hovered component on click too (fixes double-click: first click missed if mouseMove throttled)
    updateHoveredComponent(e);
    if (hoveredComponent != nullptr) {
      auto paramID = hoveredComponent->getProperties()["paramID"].toString();
      if (paramID.isNotEmpty()) {
        if (manager.isParameterMapped(paramID)) {
          manager.removeMappingForParam(paramID);
        } else {
          manager.setSelectedParameterForLearning(paramID);
        }
        mapList.updateContent();
        repaint();
      }
    }
  }

  void mouseMove(const juce::MouseEvent &e) override {
    if (isVisible()) {
      auto now = juce::Time::getMillisecondCounter();
      if (now - lastHoverUpdateMs >= 30) {  // 30ms for more responsive hover overlay
        lastHoverUpdateMs = now;
        updateHoveredComponent(e);
      }
    }
  }

private:
  juce::uint32 lastHoverUpdateMs = 0;
  MidiMappingService &manager;
  juce::Component &rootContent;
  juce::ListBox mapList;
  MappingListModel listModel;
  juce::TextButton btnClearAll;
  juce::TextButton btnSearch;
  juce::TextButton btnDone;
  juce::TextButton btnMoveList;
  bool listOnRight_ = true;  // true = list on right (default), false = list on left
  juce::Component *hoveredComponent = nullptr;

  juce::Rectangle<int> logArea, learnBtnArea;

  juce::Component *findComponentUnderMouse(juce::Point<int> pt) {
    // Exclude overlay so we find mappable controls underneath
    for (int i = rootContent.getNumChildComponents(); --i >= 0;) {
      auto *child = rootContent.getChildComponent(i);
      if (child == nullptr || !child->isVisible())
        continue;
      if (child == this)
        continue;
      if (child->getBounds().contains(pt)) {
        auto local = pt - child->getPosition();
        if (child->contains(local)) {
          auto *deep = child->getComponentAt(local);
          return deep ? deep : child;
        }
      }
    }
    return nullptr;
  }

  juce::Component *findComponentWithParamID(const juce::String &paramID) {
    return findRecursive(rootContent, paramID);
  }

  juce::Component *findRecursive(juce::Component &root,
                                 const juce::String &id) {
    if (root.getProperties()["paramID"].toString() == id)
      return &root;
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
      if (auto *found = findRecursive(*root.getChildComponent(i), id))
        return found;
    }
    return nullptr;
  }

  void updateHoveredComponent(const juce::MouseEvent &e) {
    // FIX: If the mouse is actually over the ListBox or Search button,
    // we shouldn't be looking for a component to "Learn" underneath.
    if (mapList.getBounds().contains(e.getPosition()) ||
        btnSearch.getBounds().contains(e.getPosition()) ||
        btnMoveList.getBounds().contains(e.getPosition()) ||
        btnDone.getBounds().contains(e.getPosition()) ||
        btnClearAll.getBounds().contains(e.getPosition())) {
      if (hoveredComponent != nullptr) {
        hoveredComponent = nullptr;
        repaint();
      }
      return;
    }

    juce::Point<int> rootPos = rootContent.getLocalPoint(this, e.getPosition());
    juce::Component *target = findComponentUnderMouse(rootPos);
    auto *scan = target;
    while (scan != nullptr && scan != &rootContent) {
      if (scan->getProperties().contains("paramID")) {
        target = scan;
        break;
      }
      scan = scan->getParentComponent();
    }
    if (target != hoveredComponent) {
      hoveredComponent = target;
      repaint();
    }
  }
};
