/*
  ==============================================================================
    Source/Core/LayoutManager.h
    Role: Placeholder for window layout save/restore and wizard flow.
    Current behaviour: layout and module visibility are handled in
    SystemController (bindLayout, applyLayout, restore from AppState) and
    AppState (window bounds, visibility). This header can be expanded to
    a dedicated LayoutManager that owns layout apply/restore/save and
    wizard steps, reducing SystemController responsibility.
  ==============================================================================
*/
#pragma once

// Layout save/restore and wizard logic currently live in SystemController
// and AppState. Future: move here and call from SystemController.

namespace LayoutManager {

// Reserved for future: applyLayout(name), saveCurrentLayout(), restoreLayout(),
// runWizardStep(step), etc.

} // namespace LayoutManager
