/*
  ==============================================================================
    Source/UI/Widgets/ModuleWindow.h
    Role: Wraps components to make them movable, resizable, SNAPPABLE.
    Features: Folding, Close button, Focus glow.
  ==============================================================================
*/
#pragma once
#include "../../Core/TimerHub.h"
#include "../Fonts.h"
#include "../PopupMenuOptions.h"
#include "../Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <string>

class ModuleWindow : public juce::Component,
                     public juce::FileDragAndDropTarget {
public:
  std::function<void()> onClose;
  std::function<bool()>
      isPlaying; // Playback-safe drag: reduce refresh when playing

  /** Optional: called when window is moved or resized so main component can
   * force full repaint (avoids ghosting). */
  std::function<void()> onMoveOrResize;
  std::function<void()>
      onDetach; // New Phase 9: Request to detach to separate native window

  juce::Component &getContent() { return content; }

  // State for persistence
  bool isFolded = false;
  int unfoldedHeight = 200;

  ModuleWindow(const juce::String &name, juce::Component &contentToWrap)
      : content(contentToWrap) {
    setName(name);
    setOpaque(true);

    // 1. Add Content
    addAndMakeVisible(content);

    // 2. Resizer (Bottom-Right)
    resizer =
        std::make_unique<juce::ResizableCornerComponent>(this, &resizeLimits);
    resizeLimits.setMinimumSize(150, 24);
    addAndMakeVisible(resizer.get());

    // 2b. Nested split divider (visible only when 2 children in 50/50 mode)
    nestedSplitDivider = std::make_unique<NestedSplitDivider>(this);
    addAndMakeVisible(nestedSplitDivider.get());
    nestedSplitDivider->setVisible(false);

    // 3. Close Button (X)
    addAndMakeVisible(btnClose);
    btnClose.setButtonText("X");
    btnClose.setColour(juce::TextButton::buttonColourId,
                       juce::Colours::transparentBlack);
    btnClose.setColour(juce::TextButton::textColourOffId,
                       Theme::text.withAlpha(0.5f));
    btnClose.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    btnClose.onClick = [this] {
      if (onClose)
        onClose();
      if (auto *p = getParentComponent())
        p->repaint();
    };
    btnClose.setTooltip("Close window (hide module)");
    btnClose.setVisible(false); // Shown on header hover

    constrainer.setMinimumOnscreenAmounts(24, 24, 24, 24);

    hubId = "Module_" + name.toStdString() + "_" +
            std::to_string(reinterpret_cast<int64_t>(this));
    TimerHub::instance().subscribe(
        hubId,
        [this]() {
          auto mousePos = getMouseXYRelative();
          bool shouldShow =
              (mousePos.y >= 0 && mousePos.y < 24 && contains(mousePos)) ||
              btnClose.isMouseOver();
          if (btnClose.isVisible() != shouldShow)
            btnClose.setVisible(shouldShow);
        },
        TimerHub::Rate10Hz);

    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
    setMouseCursor(juce::MouseCursor::NormalCursor);
  }

  ~ModuleWindow() override {
    TimerHub::instance().unsubscribe(hubId);
    s_selectedWindows.erase(this);
  }

  // --- INTERACTION ---
  void mouseDown(const juce::MouseEvent &e) override {
    cleanupSelectedWindows();
    toFront(true);
    // Tab bar (when 2+ nested, not in 50/50 mode): click switches visible child
    if (!isFolded && e.y >= 24 && e.y < 24 + tabBarHeight &&
        !(nestedChildCount() == 2 && nestedHorizontalSplit50_)) {
      juce::Array<juce::Component *> nestedList;
      for (auto *c : getChildren()) {
        if (c != &content && dynamic_cast<ModuleWindow *>(c) != nullptr)
          nestedList.add(c);
      }
      if (nestedList.size() > 1) {
        float tabW = (getWidth() - 4.0f) / (int)nestedList.size();
        int idx = (int)((e.x - 2) / tabW);
        if (idx >= 0 && idx < nestedList.size()) {
          selectedNestedIndex = idx;
          resized();
          repaint();
          return;
        }
      }
    }
    if (e.y < 24) {
      if (e.mods.isRightButtonDown()) {
        showWindowMenu();
        return;
      }
      // Require a few pixels movement before starting drag (reduces accidental
      // drag when reaching for mixer dials)
      pendingDrag = true;
      dragStartScreenPos = e.getScreenPosition();
      lastDragScreenPos = e.getScreenPosition();

      if (isPlaying && isPlaying()) {
        setBufferedToImage(true);
        reducedRefreshMode = true;
      }

      // Multi-select: Ctrl/Cmd+click adds; Shift+click toggles
      if (e.mods.isShiftDown()) {
        if (s_selectedWindows.count(this))
          s_selectedWindows.erase(this);
        else
          s_selectedWindows.insert(this);
      } else if (e.mods.isCtrlDown() || e.mods.isCommandDown()) {
        s_selectedWindows.insert(this);
      } else if (!s_selectedWindows.count(this)) {
        s_selectedWindows.clear();
        s_selectedWindows.insert(this);
      }
      for (auto *w : s_selectedWindows)
        w->repaint();

      dragger.startDraggingComponent(this, e);
    }
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    // Drag threshold: don't start moving until pointer has moved a few pixels
    // (avoids accidental drag near controls)
    if (pendingDrag) {
      float dist =
          (float)e.getScreenPosition().getDistanceFrom(dragStartScreenPos);
      if (dist < 5.0f)
        return;
      pendingDrag = false;
      dragging = true;
      lastDragScreenPos = e.getScreenPosition();
    }
    if (!dragging)
      return;
    cleanupSelectedWindows();

    for (auto it = s_selectedWindows.begin(); it != s_selectedWindows.end();) {
      auto *w = *it;
      if (w == nullptr || !w->isShowing() ||
          w->getParentComponent() != getParentComponent()) {
        it = s_selectedWindows.erase(it);
      } else {
        ++it;
      }
    }

    auto now = e.getScreenPosition();
    int dx = now.x - lastDragScreenPos.x;
    int dy = now.y - lastDragScreenPos.y;
    lastDragScreenPos = now;

    if (s_selectedWindows.size() > 1 && s_selectedWindows.count(this)) {
      auto *parent = getParentComponent();
      if (!parent)
        return;

      juce::Rectangle<int> dirtyRegion;
      for (auto *w : s_selectedWindows) {
        if (w && w->getParentComponent() == parent)
          dirtyRegion = dirtyRegion.getUnion(w->getBounds().expanded(8));
      }

      s_batchMoving = true;
      for (auto *w : s_selectedWindows) {
        if (w && w->getParentComponent() == parent)
          w->setTopLeftPosition(w->getX() + dx, w->getY() + dy);
      }
      s_batchMoving = false;

      for (auto *w : s_selectedWindows) {
        if (w && w->getParentComponent() == parent)
          dirtyRegion = dirtyRegion.getUnion(w->getBounds().expanded(8));
      }

      // Always repaint parent so vacated area is cleared (fixes ghosting when
      // reducedRefreshMode)
      parent->repaint(dirtyRegion);
      if (onMoveOrResize)
        onMoveOrResize();
      // Full repaint so Pro/OpenGL mode redraws entire framebuffer and clears
      // ghost trails
      parent->repaint();
    } else {
      auto *parent = getParentComponent();
      juce::Rectangle<int> oldBounds = getBoundsInParent().expanded(8);
      dragger.dragComponent(this, e, &constrainer);
      snapToSiblings();
      if (parent) {
        parent->repaint(oldBounds.getUnion(getBoundsInParent().expanded(8)));
        // Full repaint so Pro/OpenGL mode redraws entire framebuffer and clears
        // ghost trails
        parent->repaint();
      }
      if (onMoveOrResize)
        onMoveOrResize();
    }
  }

  void moved() override {
    if (s_batchMoving)
      return;
    juce::Component::moved();
  }

  void mouseDoubleClick(const juce::MouseEvent &e) override {
    if (e.y < 24) {
      // When nested: double-click on header toggles host 50/50 split (e.g. Log
      // | Playlist)
      auto *host = dynamic_cast<ModuleWindow *>(getParentComponent());
      if (host && host->nestedChildCount() == 2) {
        host->toggleNestedLayout50_50();
        return;
      }
      toggleFold();
    }
  }

  void mouseUp(const juce::MouseEvent &) override {
    bool wasDragging = dragging;
    if (dragging) {
      checkForDocking();
      snapToGrid();
    }
    dragging = false;
    pendingDrag = false;
    if (reducedRefreshMode) {
      reducedRefreshMode = false;
      setBufferedToImage(false);
      repaint();
    }
    if (auto *parent = getParentComponent()) {
      parent->repaint();
      if (onMoveOrResize)
        onMoveOrResize();
      if (wasDragging) {
        juce::Component::SafePointer<juce::Component> safeParent(parent);
        juce::Timer::callAfterDelay(16, [safeParent] {
          if (safeParent)
            safeParent->repaint();
        });
      }
    }
  }

  void mouseMove(const juce::MouseEvent &e) override {
    auto r = getLocalBounds();
    bool onHeader = e.y < 24;
    bool onResizeGrip = onHeader && e.x >= r.getRight() - 14;
    if (onResizeGrip)
      setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (onHeader)
      setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
      setMouseCursor(juce::MouseCursor::NormalCursor);
  }

  void showWindowMenu() {
    juce::PopupMenu m;
    m.addSectionHeader(getName());
    m.addItem("Bring to front", [this] { toFront(true); });
    m.addItem("Fold / Unfold", [this] { toggleFold(); });
    m.addItem("Reset Size", [this] {
      setSize(400, unfoldedHeight > 50 ? unfoldedHeight : 200);
    });
    m.addSeparator();
    bool nested =
        (dynamic_cast<ModuleWindow *>(getParentComponent()) != nullptr);
    if (nested) {
      m.addItem("Move to main area", [this] { unnestToMainArea(); });
      m.addSeparator();
    }
    // Phase 9: Detach
    m.addItem("Detach window", [this] {
      if (onDetach)
        onDetach();
    });
    m.addItem("Close", [this] {
      if (onClose)
        onClose();
    });
    m.showMenuAsync(PopupMenuOptions::forComponent(this));
  }

  /** Move this window out of a nested parent back to the main dashboard (call
   * from menu). */
  void unnestToMainArea() {
    auto *parent = getParentComponent();
    if (!parent)
      return;
    auto *host = dynamic_cast<ModuleWindow *>(parent);
    if (!host)
      return; // already top-level
    auto *mainComp = host->getParentComponent();
    if (!mainComp)
      return;
    juce::Point<int> topLeft =
        mainComp->getLocalPoint(this, juce::Point<int>(0, 0));
    parent->removeChildComponent(this);
    mainComp->addAndMakeVisible(this);
    setBounds(topLeft.x, topLeft.y, getWidth(), getHeight());
    toFront(true);
    host->resized(); // Re-layout host so it doesn't go blank (show content /
                     // remaining nested)
    mainComp->repaint();
  }

  bool isInterestedInFileDrag(const juce::StringArray &files) override {
    if (auto *target = dynamic_cast<juce::FileDragAndDropTarget *>(&content))
      return target->isInterestedInFileDrag(files);
    return false;
  }

  void filesDropped(const juce::StringArray &files, int x, int y) override {
    if (auto *target = dynamic_cast<juce::FileDragAndDropTarget *>(&content)) {
      auto local = content.getLocalPoint(this, juce::Point<int>(x, y));
      target->filesDropped(files, local.x, local.y);
    }
  }

  /** Called from main window: select all ModuleWindows that are direct children
   * of parent and intersect rect. */
  static void selectWindowsInRect(juce::Component *parent,
                                  juce::Rectangle<int> rect) {
    if (!parent)
      return;
    s_selectedWindows.clear();
    for (auto *c : parent->getChildren()) {
      auto *win = dynamic_cast<ModuleWindow *>(c);
      if (win && win->isVisible() && rect.intersects(win->getBounds()))
        s_selectedWindows.insert(win);
    }
    for (auto *w : s_selectedWindows)
      if (w)
        w->repaint();
  }
  /** Move all selected windows that are direct children of parent by (dx, dy).
   */
  static void moveSelectedWindows(juce::Component *parent, int dx, int dy) {
    if (!parent || (dx == 0 && dy == 0))
      return;
    for (auto *w : s_selectedWindows) {
      if (w && w->getParentComponent() == parent)
        w->setTopLeftPosition(w->getX() + dx, w->getY() + dy);
    }
    if (parent)
      parent->repaint();
  }
  /** Whether any windows are currently selected (for multi-move from
   * background). */
  static bool hasSelection() { return !s_selectedWindows.empty(); }
  /** Clear selection without repainting (e.g. when starting new marquee without
   * Shift). */
  static void clearSelection() { s_selectedWindows.clear(); }

  /** Number of direct child ModuleWindows (for double-click 50/50). */
  int nestedChildCount() const {
    int n = 0;
    for (auto *c : getChildren())
      if (c != &content && dynamic_cast<ModuleWindow *>(c) != nullptr)
        ++n;
    return n;
  }
  void toggleNestedLayout50_50() {
    nestedHorizontalSplit50_ = !nestedHorizontalSplit50_;
    resized();
    repaint();
  }
  /** Split ratio for two nested children (0.15–0.85). Default 0.5 = 50/50. */
  void setNestedSplitRatio(float ratio) {
    nestedSplitRatio_ = juce::jlimit(0.15f, 0.85f, ratio);
  }
  float getNestedSplitRatio() const { return nestedSplitRatio_; }

  void focusGained(juce::Component::FocusChangeType) override { repaint(); }
  void focusLost(juce::Component::FocusChangeType) override {
    // Safety: reset reducedRefreshMode if stuck (e.g. mouse released outside)
    if (reducedRefreshMode) {
      reducedRefreshMode = false;
      dragging = false;
      pendingDrag = false;
      setBufferedToImage(false);
      repaint();
    }
    repaint();
  }

  // --- FOLDING ---
  void toggleFold() {
    auto &animator = juce::Desktop::getInstance().getAnimator();
    auto target = getBounds();

    if (isFolded) {
      target.setHeight(unfoldedHeight > 50 ? unfoldedHeight : 200);
      animator.animateComponent(this, target, 1.0f, 250, false, 0.0, 0.0);
      content.setVisible(true);
      resizer->setVisible(true);
      isFolded = false;
    } else {
      unfoldedHeight = getHeight();
      target.setHeight(24);
      animator.animateComponent(this, target, 1.0f, 250, false, 0.0, 0.0);
      content.setVisible(false);
      resizer->setVisible(false);
      isFolded = true;
    }
  }

  // --- PAINTING ---
  void paint(juce::Graphics &g) override {
    auto r = getLocalBounds().toFloat();
    bool focused = hasKeyboardFocus(true);
    bool nested =
        (dynamic_cast<ModuleWindow *>(getParentComponent()) != nullptr);

    // 0. Solid background first (fixes OpenGL ghosting)
    g.fillAll(Theme::bgPanel);

    // 1. Multi-layer drop shadow (softer, more depth)
    const float shadowOffset = 5.0f;
    for (int i = 3; i >= 1; --i) {
      float o = shadowOffset * (float)i * 0.6f;
      float a = 0.14f - (float)i * 0.03f;
      g.setColour(juce::Colours::black.withAlpha(a));
      g.fillRoundedRectangle(r.translated(o, o), 8.0f + (float)i);
    }

    // 1. Body Background (Glassy)
    if (!isFolded) {
      g.setColour(Theme::bgPanel.withAlpha(0.96f));
      g.fillRoundedRectangle(r, 6.0f);
    }

    // 2. Header
    auto header = r.removeFromTop(24.0f);
    juce::Colour headerCol =
        focused ? Theme::bgDark.brighter(0.15f) : Theme::bgDark;
    if (nested)
      headerCol = headerCol.interpolatedWith(
          Theme::accent,
          0.12f); // Nested: subtle tint so host is distinguishable

    g.setColour(headerCol);
    g.fillRoundedRectangle(header.getX(), header.getY(), header.getWidth(),
                           isFolded ? 6.0f : 10.0f, 6.0f);

    if (!isFolded)
      g.fillRect(header.withTop(15.0f));

    // 2b. Per-window accent line at top (different shade per module type)
    juce::Colour windowAccent = getAccentForName(getName());
    g.setColour(windowAccent.withAlpha(0.5f));
    g.fillRect(header.getX(), header.getY(), header.getWidth(), 2.0f);

    // 3. Title: breadcrumb when nested (Host › This), else just name
    juce::String titleText = getName();
    if (nested) {
      if (auto *host = dynamic_cast<ModuleWindow *>(getParentComponent()))
        titleText = host->getName() + " \u203A " + getName(); // ›
    }
    g.setColour(focused ? windowAccent : Theme::text.withAlpha(0.7f));
    g.setFont(Fonts::bodyBold().withHeight(13.0f));
    auto titleArea = header.reduced(10, 0);
    if (nested)
      titleArea.removeFromLeft(4); // Make room for nested bar
    g.drawText(titleText, titleArea, juce::Justification::centredLeft, true);

    // 3b. Nested indicator: left edge bar in this window's accent
    if (nested) {
      g.setColour(windowAccent.withAlpha(0.55f));
      g.fillRect(0.0f, 24.0f, 3.0f, (float)getHeight() - 24.0f);
    }

    // 3c. Tab bar when 2+ nested (hidden in 50/50 split mode)
    juce::Array<juce::Component *> nestedList;
    for (auto *c : getChildren()) {
      if (c != &content && dynamic_cast<ModuleWindow *>(c) != nullptr)
        nestedList.add(c);
    }
    if (nestedList.size() > 1 &&
        !(nestedList.size() == 2 && nestedHorizontalSplit50_)) {
      auto tabStrip = juce::Rectangle<float>(0.0f, 24.0f, (float)getWidth(),
                                             (float)tabBarHeight);
      g.setColour(Theme::bgDark.darker(0.1f));
      g.fillRect(tabStrip);
      float tabW = (getWidth() - 4.0f) / (int)nestedList.size();
      for (int i = 0; i < nestedList.size(); ++i) {
        auto *child = dynamic_cast<ModuleWindow *>(nestedList[i]);
        if (!child)
          continue;
        juce::Colour tabAccent = getAccentForName(child->getName());
        auto tabR = juce::Rectangle<float>(2.0f + i * tabW, 24.0f, tabW - 1.0f,
                                           (float)tabBarHeight);
        bool sel = (i == selectedNestedIndex);
        g.setColour(sel ? tabAccent.withAlpha(0.25f)
                        : tabAccent.withAlpha(0.08f));
        g.fillRect(tabR);
        g.setColour(sel ? tabAccent : Theme::text.withAlpha(0.6f));
        g.setFont(Fonts::small().withHeight(11.0f));
        g.drawText(child->getName(), tabR.reduced(4.0f),
                   juce::Justification::centredLeft, true);
      }
    }

    // 4. Inner border (1px inset) — gives a visible seam when windows are
    // snapped together
    auto inner = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.drawRoundedRectangle(inner, 5.0f, 1.0f);

    // 5. Active Glow Border (or multi-select highlight)
    if (focused) {
      g.setColour(Theme::accent.withAlpha(0.4f));
      g.drawRoundedRectangle(getLocalBounds().toFloat(), 6.0f, 1.5f);
    } else if (s_selectedWindows.size() > 1 && s_selectedWindows.count(this)) {
      g.setColour(Theme::accent.withAlpha(0.25f));
      g.drawRoundedRectangle(getLocalBounds().toFloat(), 6.0f, 1.0f);
    } else {
      g.setColour(juce::Colours::black.withAlpha(0.3f));
      g.drawRoundedRectangle(getLocalBounds().toFloat(), 6.0f, 1.0f);
    }
  }

  void resized() override {
    auto r = getLocalBounds();
    btnClose.setBounds(r.removeFromRight(24).removeFromTop(24).reduced(4));

    if (isFolded) {
      // When folded: show resize grip on right edge of header for toolbar width
      resizer->setBounds(r.getRight() - 14, 0, 14, 24);
      resizer->setVisible(true);
      resizer->toFront(false);
    } else {
      r.removeFromTop(24);
      juce::Array<juce::Component *> nested;
      for (auto *c : getChildren()) {
        if (c != &content && dynamic_cast<ModuleWindow *>(c) != nullptr)
          nested.add(c);
      }

      if (nested.size() == 2 && nestedHorizontalSplit50_) {
        // Double-click 50/50: two children side by side with draggable divider
        // (e.g. Log | Playlist)
        content.setVisible(false);
        const int dividerW = 6;
        int contentTotal = juce::jmax(0, getWidth() - 4 - dividerW);
        int leftW = (int)(contentTotal * nestedSplitRatio_ + 0.5f);
        leftW = juce::jlimit(0, contentTotal, leftW);
        int contentH = juce::jmax(0, getHeight() - 26);
        nested[0]->setBounds(2, 24, leftW, contentH);
        nested[0]->setVisible(true);
        nestedSplitDivider->setBounds(2 + leftW, 24, dividerW, contentH);
        nestedSplitDivider->setVisible(true);
        nestedSplitDivider->toFront(false);
        nested[1]->setBounds(2 + leftW + dividerW, 24, contentTotal - leftW,
                             contentH);
        nested[1]->setVisible(true);
      } else {
        nestedSplitDivider->setVisible(false);
        if (nested.size() > 1) {
          // Tab bar mode: one visible child, tab strip with per-window accent
          // shades
          if (selectedNestedIndex >= nested.size())
            selectedNestedIndex = 0;
          int belowTabs = 24 + tabBarHeight;
          int contentH = juce::jmax(0, getHeight() - belowTabs - 2);
          content.setVisible(false);
          for (int i = 0; i < nested.size(); ++i) {
            bool visible = (i == selectedNestedIndex);
            nested[i]->setVisible(visible);
            if (visible)
              nested[i]->setBounds(2, belowTabs,
                                   juce::jmax(150, getWidth() - 4), contentH);
          }
        } else if (!nested.isEmpty()) {
          // Single nested: stack below header (Bitwig-style), min 120px
          content.setVisible(true);
          int contentY = 24;
          int remainingH = juce::jmax(0, getHeight() - 24 - 2);
          const int minNestedH = 120;
          int nestH = juce::jmax(minNestedH, remainingH);
          nested[0]->setBounds(2, contentY, juce::jmax(150, getWidth() - 4),
                               nestH);
          contentY += nestH + 4;
          content.setBounds(2, contentY, juce::jmax(0, getWidth() - 4),
                            juce::jmax(0, getHeight() - contentY - 2));
        } else {
          content.setVisible(true);
          content.setBounds(2, 24, juce::jmax(0, getWidth() - 4),
                            juce::jmax(0, getHeight() - 26));
        }
      }
      resizer->setBounds(getWidth() - 14, getHeight() - 14, 14, 14);
      resizer->setVisible(true);
      resizer->toFront(false); // Resize grip on top so it's always clickable
                               // (nested/full windows)
    }

    // Force repaint after layout so host and content/nested don't go blank when
    // resizing
    repaint();
    if (content.isVisible())
      content.repaint();
    for (auto *c : getChildren()) {
      if (c != &content && c != resizer.get() && nestedSplitDivider &&
          c != nestedSplitDivider.get()) {
        if (auto *nw = dynamic_cast<ModuleWindow *>(c))
          if (nw->isVisible())
            nw->repaint();
      }
    }
    if (onMoveOrResize)
      onMoveOrResize();
  }

private:
  /** Draggable divider between two nested children in 50/50 split mode. */
  struct NestedSplitDivider : public juce::Component {
    explicit NestedSplitDivider(ModuleWindow *host_) : host(host_) {
      setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
      setInterceptsMouseClicks(true, true);
    }
    void paint(juce::Graphics &g) override {
      auto r = getLocalBounds().toFloat();
      g.fillAll(Theme::bgPanel.darker(0.2f));
      g.setColour(Theme::accent.withAlpha(0.4f));
      g.fillRect(r.withWidth(1.0f).withCentre(r.getCentre()));
    }
    void mouseDown(const juce::MouseEvent &e) override {
      juce::ignoreUnused(e);
    }
    void mouseDrag(const juce::MouseEvent &e) override {
      if (!host)
        return;
      auto pos = e.getEventRelativeTo(host).getPosition();
      int contentW = host->getWidth() - 4 - getWidth();
      if (contentW <= 0)
        return;
      float ratio = (float)(pos.x - 2) / (float)contentW;
      ratio = juce::jlimit(0.15f, 0.85f, ratio);
      host->setNestedSplitRatio(ratio);
    }
    ModuleWindow *host = nullptr;
  };

  std::string hubId;
  juce::Component &content;
  std::unique_ptr<juce::ResizableCornerComponent> resizer;
  juce::ComponentBoundsConstrainer resizeLimits;
  juce::ComponentDragger dragger;
  juce::ComponentBoundsConstrainer constrainer;
  juce::TextButton btnClose;
  bool dragging = false;
  bool pendingDrag = false;
  juce::Point<int> dragStartScreenPos;
  bool reducedRefreshMode = false;
  juce::Point<int> lastDragScreenPos;
  int selectedNestedIndex = 0; // When 2+ nested children, which tab is visible
  bool nestedHorizontalSplit50_ =
      false; // When true and 2 nested children, layout side-by-side 50/50
  float nestedSplitRatio_ =
      0.5f; // Left pane width ratio when 2 nested in 50/50 (0.15–0.85)
  std::unique_ptr<NestedSplitDivider> nestedSplitDivider;

  static inline std::set<ModuleWindow *> s_selectedWindows;
  static inline bool s_batchMoving = false;

  static void cleanupSelectedWindows() {
    for (auto it = s_selectedWindows.begin(); it != s_selectedWindows.end();) {
      if (*it == nullptr || !(*it)->isVisible()) {
        it = s_selectedWindows.erase(it);
      } else {
        ++it;
      }
    }
  }

  static constexpr int snapThreshold = 18; // Edge snap threshold
  static constexpr int dockSnapMargin = 5; // Dock target
  static constexpr int gridSize = 2; // Fine grid for precise module placement
  static constexpr int centerSnapThreshold = 15; // Center alignment snap
  static constexpr int snapGap =
      2; // Pixel gap between snapped windows (no overlap)
  static constexpr int tabBarHeight = 22; // Height of nested-module tab strip

  /** Per-window accent colour (different shade per module type for quick
   * recognition). */
  static juce::Colour getAccentForName(const juce::String &name) {
    if (name == "Editor")
      return juce::Colour(0xff00a3ff); // blue (theme default)
    if (name == "Mixer")
      return juce::Colour(0xff00c853); // green
    if (name == "Sequencer")
      return juce::Colour(0xff9c27b0); // purple
    if (name == "Playlist")
      return juce::Colour(0xffff9800); // orange
    if (name == "OSC Log" || name == "Log")
      return juce::Colour(0xff795548); // brown
    if (name == "Arpeggiator" || name == "Arp")
      return juce::Colour(0xff00bcd4); // cyan
    if (name == "Macros")
      return juce::Colour(0xffe91e63); // pink
    if (name == "Chords")
      return juce::Colour(0xff8bc34a); // light green
    return Theme::accent;
  }

  // Robust hit testing using screen coordinates (accurate regardless of
  // hierarchy)
  void checkForDocking() {
    auto *parent = getParentComponent();
    if (!parent)
      return;

    auto mousePos = juce::Desktop::getMousePosition();

    // Unnest only via right-click menu "Move to main area" (not drop on host
    // header)
    auto *host = dynamic_cast<ModuleWindow *>(parent);

    for (auto *sibling : parent->getChildren()) {
      auto *targetWin = dynamic_cast<ModuleWindow *>(sibling);
      if (!targetWin || targetWin == this || !targetWin->isVisible())
        continue;
      // When we're nested, don't nest into a sibling (avoid confusion); only
      // unnest via host header
      if (host && targetWin != host)
        continue;

      auto targetScreenBounds = targetWin->getScreenBounds();
      auto targetHeader = targetScreenBounds.withHeight(24);

      // If dropped strictly ON header, nest inside target (module-in-module)
      if (targetHeader.contains(mousePos)) {
        attemptNestInto(targetWin);
        return;
      }
      // Snap-to-right disabled: user requested no auto-snap when moving over
      // another module
    }
  }

  /** Nest this window inside target (below target's header). Target lays out
   * nested windows in resized(). */
  void attemptNestInto(ModuleWindow *target) {
    auto *parent = getParentComponent();
    if (!parent || target == this || target == parent)
      return;
    juce::Point<int> posInTarget(2, 26);
    if (auto *p = getParentComponent())
      p->removeChildComponent(this);
    target->addChildComponent(this);
    setBounds(posInTarget.x, posInTarget.y,
              juce::jmax(150, target->getWidth() - 4), 180);
    target->repaint();
  }

  // Snap this window next to target (to the right), with gap so they don't
  // overlap
  void snapToTarget(ModuleWindow *target) {
    auto targetBounds = target->getBounds();
    setTopLeftPosition(targetBounds.getRight() + snapGap, targetBounds.getY());
  }

  void snapToGrid() {
    auto *parent = getParentComponent();
    if (!parent)
      return;
    int x = getX();
    int y = getY();
    int g = gridSize;
    int nx = ((x + g / 2) / g) * g;
    int ny = ((y + g / 2) / g) * g;
    if (nx != x || ny != y)
      setTopLeftPosition(nx, ny);
  }

  void snapToSiblings() {
    auto *parent = getParentComponent();
    if (!parent)
      return;

    // Use screen bounds for accurate overlap detection
    auto myScreen = getScreenBounds();
    int x = myScreen.getX();
    int y = myScreen.getY();
    int r = myScreen.getRight();
    int b = myScreen.getBottom();

    bool snappedX = false;
    bool snappedY = false;

    // --- CENTER ALIGNMENT (Photoshop-style magnetic guides) ---
    int parentCenterX = parent->getWidth() / 2;
    int parentCenterY = parent->getHeight() / 2;
    int myCenterX = getX() + getWidth() / 2;
    int myCenterY = getY() + getHeight() / 2;

    if (std::abs(myCenterX - parentCenterX) < centerSnapThreshold) {
      x = myScreen.getX() + (parentCenterX - myCenterX);
      snappedX = true;
    }
    if (std::abs(myCenterY - parentCenterY) < centerSnapThreshold) {
      y = myScreen.getY() + (parentCenterY - myCenterY);
      snappedY = true;
    }

    // --- SIBLING EDGE SNAPPING ---
    for (auto *sibling : parent->getChildren()) {
      if (sibling == this || !sibling->isVisible())
        continue;

      auto *otherWin = dynamic_cast<ModuleWindow *>(sibling);
      if (!otherWin)
        continue;

      auto otherScreen = otherWin->getScreenBounds();

      if (!snappedX) {
        if (std::abs(x - otherScreen.getRight()) < snapThreshold) {
          x = otherScreen.getRight() + snapGap;
          snappedX = true;
        } else if (std::abs(r - otherScreen.getX()) < snapThreshold) {
          x = otherScreen.getX() - getWidth() - snapGap;
          snappedX = true;
        } else if (std::abs(x - otherScreen.getX()) < snapThreshold) {
          x = otherScreen.getX(); // align left edges (no gap)
          snappedX = true;
        } else if (std::abs(r - otherScreen.getRight()) < snapThreshold) {
          x = otherScreen.getRight() - getWidth(); // align right edges (no gap)
          snappedX = true;
        }
      }

      if (!snappedY) {
        if (std::abs(y - otherScreen.getBottom()) < snapThreshold) {
          y = otherScreen.getBottom() + snapGap; // we're below them, gap
          snappedY = true;
        } else if (std::abs(b - otherScreen.getY()) < snapThreshold) {
          y = otherScreen.getY() - getHeight() -
              snapGap; // we're above them, gap
          snappedY = true;
        } else if (std::abs(y - otherScreen.getY()) < snapThreshold) {
          y = otherScreen.getY(); // align tops
          snappedY = true;
        } else if (std::abs(b - otherScreen.getBottom()) < snapThreshold) {
          y = otherScreen.getBottom() - getHeight(); // align bottoms
          snappedY = true;
        }
      }

      if (snappedX && snappedY)
        break;
    }

    // Convert back to parent-local coordinates
    auto parentScreen = parent->getScreenBounds();
    setTopLeftPosition(x - parentScreen.getX(), y - parentScreen.getY());
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleWindow)
};
