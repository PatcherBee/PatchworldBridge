#pragma once
#include "../Services/MidiMappingService.h"
#include "../UI/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * ParameterSearchOverlay - A "Command Palette" style overlay for MIDI mapping.
 * Allows users to search and select parameters by name (e.g., "Mixer", "Macro")
 * rather than clicking on the GUI. Useful for mapping parameters that aren't
 * currently visible.
 */
class ParameterSearchOverlay : public juce::Component,
                               public juce::TextEditor::Listener,
                               public juce::ListBoxModel {
public:
  // Callback when a parameter is selected for learning
  std::function<void(const juce::String &)> onParameterSelected;

  ParameterSearchOverlay(MidiMappingService &m) : manager(m) {
    // Search Box
    searchBox.setTextToShowWhenEmpty(
        "Search parameters (e.g., 'Mixer', 'Macro', 'Vol')...",
        juce::Colours::grey);
    searchBox.addListener(this);
    searchBox.setColour(juce::TextEditor::backgroundColourId,
                        juce::Colours::black.withAlpha(0.8f));
    searchBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    searchBox.setColour(juce::TextEditor::outlineColourId,
                        Theme::accent.withAlpha(0.5f));
    searchBox.setColour(juce::TextEditor::focusedOutlineColourId,
                        Theme::accent);
    addAndMakeVisible(searchBox);

    // Results List
    resultsList.setModel(this);
    resultsList.setColour(juce::ListBox::backgroundColourId,
                          juce::Colours::black.withAlpha(0.7f));
    resultsList.setColour(juce::ListBox::outlineColourId,
                          Theme::accent.withAlpha(0.3f));
    resultsList.setRowHeight(28);
    addAndMakeVisible(resultsList);

    // Close Button
    btnClose.setButtonText("X");
    btnClose.setColour(juce::TextButton::buttonColourId,
                       juce::Colours::red.darker(0.5f));
    btnClose.onClick = [this] { setVisible(false); };
    addAndMakeVisible(btnClose);

    // Status Label
    lblStatus.setColour(juce::Label::textColourId,
                        juce::Colours::white.withAlpha(0.6f));
    lblStatus.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(lblStatus);

    // Initial population
    updateFilteredList("");
  }

  // --- ListBoxModel Implementation ---
  int getNumRows() override { return filteredParams.size(); }

  void paintListBoxItem(int row, juce::Graphics &g, int w, int h,
                        bool selected) override {
    if (row < 0 || row >= filteredParams.size())
      return;

    // Background
    if (selected) {
      g.setColour(Theme::accent.withAlpha(0.4f));
      g.fillRoundedRectangle(2.0f, 2.0f, w - 4.0f, h - 4.0f, 4.0f);
    }

    // Check if already mapped
    bool isMapped = manager.isParameterMapped(filteredParams[row]);

    // Icon indicator
    g.setColour(isMapped ? juce::Colours::lime : juce::Colours::grey);
    g.fillEllipse(8.0f, (h - 8.0f) / 2.0f, 8.0f, 8.0f);

    // Parameter name
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(filteredParams[row], 24, 0, w - 30, h,
               juce::Justification::centredLeft);
  }

  void listBoxItemClicked(int row, const juce::MouseEvent &) override {
    if (row < 0 || row >= filteredParams.size())
      return;

    auto selectedParamID = filteredParams[row];

    // 1. Enter Learn Mode for this specific parameter
    manager.setSelectedParameterForLearning(selectedParamID);

    // 2. Fire callback
    if (onParameterSelected)
      onParameterSelected(selectedParamID);

    // 3. Update status
    lblStatus.setText("Waiting for MIDI input for: " + selectedParamID,
                      juce::dontSendNotification);
  }

  void listBoxItemDoubleClicked(int row, const juce::MouseEvent &e) override {
    // Pass the existing event 'e' instead of constructing a new one
    listBoxItemClicked(row, e);
    setVisible(false);
  }

  // --- TextEditor::Listener Implementation ---
  void textEditorTextChanged(juce::TextEditor &ed) override {
    updateFilteredList(ed.getText());
  }

  void textEditorEscapeKeyPressed(juce::TextEditor &) override {
    setVisible(false);
  }

  void updateFilteredList(const juce::String &query) {
    filteredParams.clear();
    auto allParams = manager.getAllMappableParameters();

    for (const auto &p : allParams) {
      if (query.isEmpty() || p.containsIgnoreCase(query))
        filteredParams.add(p);
    }

    resultsList.updateContent();
    resultsList.repaint();

    lblStatus.setText(juce::String(filteredParams.size()) + " parameters found",
                      juce::dontSendNotification);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(10);

    // Close button top-right
    btnClose.setBounds(r.removeFromRight(30).removeFromTop(30));

    // Search box at top
    searchBox.setBounds(r.removeFromTop(35));
    r.removeFromTop(5);

    // Status at bottom
    lblStatus.setBounds(r.removeFromBottom(20));
    r.removeFromBottom(5);

    // Results list fills the rest
    resultsList.setBounds(r);
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();

    // Semi-transparent dark background
    g.setColour(juce::Colours::black.withAlpha(0.9f));
    g.fillRoundedRectangle(bounds, 8.0f);

    // Accent border
    g.setColour(Theme::accent.withAlpha(0.6f));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 8.0f, 2.0f);

    // Title
    g.setColour(Theme::accent);
    g.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    g.drawText("PARAMETER SEARCH", bounds.removeFromTop(35),
               juce::Justification::centred);
  }

  void visibilityChanged() override {
    if (isVisible()) {
      // Focus the search box when shown
      searchBox.clear();
      searchBox.grabKeyboardFocus();
      updateFilteredList("");
    }
  }

  // Refresh parameter list from manager
  void refresh() { updateFilteredList(searchBox.getText()); }

private:
  MidiMappingService &manager;
  juce::TextEditor searchBox;
  juce::ListBox resultsList;
  juce::StringArray filteredParams;
  juce::TextButton btnClose;
  juce::Label lblStatus;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterSearchOverlay)
};
