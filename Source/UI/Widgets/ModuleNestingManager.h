/*
  ==============================================================================
    Source/UI/Widgets/ModuleNestingManager.h
    Role: Manages drag-and-drop nesting of ModuleWindows (module-in-module)
  ==============================================================================
*/
#pragma once
#include "ModuleWindow.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>

class ModuleNestingManager {
public:
  struct NestingInfo {
    ModuleWindow* parent = nullptr;
    std::vector<ModuleWindow*> children;
    juce::Rectangle<int> splitBounds; // Bounds within parent for nested view
    bool isHorizontalSplit = true;
  };
  
  static ModuleNestingManager& getInstance() {
    static ModuleNestingManager instance;
    return instance;
  }
  
  // Check if a module can be nested into another
  bool canNestInto(ModuleWindow* child, ModuleWindow* parent) {
    if (!child || !parent || child == parent)
      return false;
    
    // Prevent circular nesting
    if (isAncestorOf(child, parent))
      return false;
    
    // Check if parent is already at max nesting depth
    if (getNestingDepth(parent) >= maxNestingDepth)
      return false;
    
    return true;
  }
  
  // Nest a module into another
  void nestModule(ModuleWindow* child, ModuleWindow* parent, bool horizontalSplit = true) {
    if (!canNestInto(child, parent))
      return;
    
    // Remove from any existing parent
    unnestModule(child);
    
    // Add to new parent
    auto& info = nestingMap[parent];
    info.parent = nullptr; // parent has no parent
    info.children.push_back(child);
    info.isHorizontalSplit = horizontalSplit;
    
    // Update child's parent info
    nestingMap[child].parent = parent;
    
    // Reparent in component hierarchy
    parent->addAndMakeVisible(child);
    
    // Layout nested modules
    layoutNestedModules(parent);
  }
  
  // Remove a module from its parent
  void unnestModule(ModuleWindow* module) {
    if (!module)
      return;
    
    auto it = nestingMap.find(module);
    if (it == nestingMap.end() || !it->second.parent)
      return;
    
    auto* parent = it->second.parent;
    auto& parentInfo = nestingMap[parent];
    
    // Remove from parent's children
    parentInfo.children.erase(
      std::remove(parentInfo.children.begin(), parentInfo.children.end(), module),
      parentInfo.children.end()
    );
    
    // Clear parent reference
    it->second.parent = nullptr;
    
    // Reparent to main component
    if (auto* mainComp = parent->getParentComponent()) {
      mainComp->addAndMakeVisible(module);
    }
    
    // Re-layout parent
    layoutNestedModules(parent);
  }
  
  // Get all children of a module
  std::vector<ModuleWindow*> getChildren(ModuleWindow* parent) {
    auto it = nestingMap.find(parent);
    if (it != nestingMap.end())
      return it->second.children;
    return {};
  }
  
  // Get parent of a module
  ModuleWindow* getParent(ModuleWindow* child) {
    auto it = nestingMap.find(child);
    if (it != nestingMap.end())
      return it->second.parent;
    return nullptr;
  }
  
  // Layout nested modules within parent
  void layoutNestedModules(ModuleWindow* parent) {
    if (!parent)
      return;
    
    auto it = nestingMap.find(parent);
    if (it == nestingMap.end() || it->second.children.empty())
      return;
    
    auto& info = it->second;
    auto bounds = parent->getLocalBounds();
    
    // Reserve space for parent's header (24px)
    bounds.removeFromTop(24);
    
    // Split bounds among children
    int numChildren = (int)info.children.size();
    if (numChildren == 0)
      return;
    
    if (info.isHorizontalSplit) {
      // Horizontal split (side-by-side)
      int childWidth = bounds.getWidth() / numChildren;
      for (int i = 0; i < numChildren; ++i) {
        auto childBounds = bounds.removeFromLeft(childWidth);
        info.children[i]->setBounds(childBounds);
      }
    } else {
      // Vertical split (stacked)
      int childHeight = bounds.getHeight() / numChildren;
      for (int i = 0; i < numChildren; ++i) {
        auto childBounds = bounds.removeFromTop(childHeight);
        info.children[i]->setBounds(childBounds);
      }
    }
  }
  
  // Check if module is nested
  bool isNested(ModuleWindow* module) {
    return getParent(module) != nullptr;
  }
  
  // Get nesting depth (0 = not nested, 1 = nested once, etc.)
  int getNestingDepth(ModuleWindow* module) {
    int depth = 0;
    auto* current = module;
    while (current && depth < maxNestingDepth) {
      current = getParent(current);
      if (current)
        ++depth;
    }
    return depth;
  }
  
private:
  std::map<ModuleWindow*, NestingInfo> nestingMap;
  static constexpr int maxNestingDepth = 3;
  
  // Check if potential child is an ancestor of potential parent (prevent circular nesting)
  bool isAncestorOf(ModuleWindow* potentialAncestor, ModuleWindow* module) {
    auto* current = module;
    while (current) {
      if (current == potentialAncestor)
        return true;
      current = getParent(current);
    }
    return false;
  }
};
