/*
  ==============================================================================
    Source/UI/Panels/NetworkConfigPanel.h
    Role: Compact OSC network config (IP, ports, Connect) for Menu dropdown.
  ==============================================================================
*/
#pragma once
#include "../../Network/LocalAddresses.h"
#include "../Theme.h"
#include "../Widgets/Indicators.h"
#include <juce_gui_basics/juce_gui_basics.h>

class NetworkConfigPanel : public juce::Component {
public:
  NetworkConfigPanel() {
    addAndMakeVisible(lblIp);
    lblIp.setText("IP:", juce::dontSendNotification);
    addAndMakeVisible(lblOut);
    lblOut.setText("Out:", juce::dontSendNotification);
    addAndMakeVisible(lblIn);
    lblIn.setText("In:", juce::dontSendNotification);

    addAndMakeVisible(edIp);
    edIp.setText("127.0.0.1");
    edIp.setJustification(juce::Justification::centred);

    addAndMakeVisible(btnLocalIps);
    btnLocalIps.setButtonText("Local");
    btnLocalIps.setTooltip("Pick a local IPv4 address (this PC or device on your network).");
    btnLocalIps.onClick = [this] {
      juce::StringArray addrs = getLocalIPv4Addresses();
      juce::PopupMenu m;
      m.addSectionHeader("Local IPv4");
      for (int i = 0; i < addrs.size(); ++i)
        m.addItem(addrs[i], [this, addr = addrs[i]] { edIp.setText(addr, juce::dontSendNotification); });
      if (addrs.isEmpty())
        m.addItem("(none)", false);
      m.showMenuAsync(juce::PopupMenu::Options()
                          .withTargetComponent(&btnLocalIps)
                          .withParentComponent(getParentComponent()));
    };

    addAndMakeVisible(edPortOut);
    edPortOut.setText("3330");
    edPortOut.setJustification(juce::Justification::centred);

    addAndMakeVisible(edPortIn);
    edPortIn.setText("5550");
    edPortIn.setJustification(juce::Justification::centred);

    addAndMakeVisible(btnConnect);
    btnConnect.setClickingTogglesState(true);
    btnConnect.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
    addAndMakeVisible(led);
    addAndMakeVisible(lblMyIp);
    lblMyIp.setJustificationType(juce::Justification::centredLeft);
    lblMyIp.setColour(juce::Label::textColourId, Theme::text);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(12);
    // My IP line along top (primary local IPv4, non-loopback when available)
    juce::StringArray addrs = getLocalIPv4Addresses();
    juce::String myIp = "127.0.0.1";
    for (int i = 0; i < addrs.size(); ++i) {
      if (addrs[i] != "127.0.0.1") {
        myIp = addrs[i];
        break;
      }
    }
    lblMyIp.setText("My IP: " + myIp, juce::dontSendNotification);
    lblMyIp.setBounds(r.removeFromTop(26));
    r.removeFromTop(4);
    auto row = r.removeFromTop(36);
    const int labelW = 32;
    const int portW = 62;
    const int gap = 10;
    lblIp.setBounds(row.removeFromLeft(labelW));
    edIp.setBounds(row.removeFromLeft(150).reduced(0, 2));
    btnLocalIps.setBounds(row.removeFromLeft(52).reduced(2));
    row.removeFromLeft(gap);
    lblOut.setBounds(row.removeFromLeft(36));
    edPortOut.setBounds(row.removeFromLeft(portW).reduced(0, 2));
    row.removeFromLeft(gap);
    lblIn.setBounds(row.removeFromLeft(28));
    edPortIn.setBounds(row.removeFromLeft(portW).reduced(0, 2));
    row.removeFromLeft(gap);
    led.setBounds(row.removeFromLeft(24).reduced(2));
    btnConnect.setBounds(row.removeFromLeft(88).reduced(2));
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgPanel);
    g.setColour(Theme::accent.withAlpha(0.4f));
    g.drawRect(getLocalBounds(), 1);
  }

  juce::Label lblMyIp{"myip", "My IP: —"};
  juce::Label lblIp{"ip", "IP:"}, lblOut{"out", "Out:"}, lblIn{"in", "In:"};
  juce::TextEditor edIp, edPortOut, edPortIn;
  juce::TextButton btnLocalIps{"Local"}, btnConnect{"Connect"};
  ConnectionLight led;
};
