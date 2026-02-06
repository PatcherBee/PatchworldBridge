/*
  ==============================================================================
    Source/Network/LocalAddresses.h
    Role: Detect local IPv4 (and optionally IPv6) addresses visible to this
          machine. Cross-platform (Win/Mac/Linux) via JUCE.
  ==============================================================================
*/
#pragma once
#include <juce_core/juce_core.h>

/** Returns IPv4 addresses this machine is using (loopback first, then others).
 *  Uses JUCE IPAddress::getAllAddresses(false) — works on Windows, macOS, Linux.
 *  Use so the user can pick their headset/laptop/PC when default IP fails.
 */
inline juce::StringArray getLocalIPv4Addresses() {
  juce::StringArray out;
  juce::Array<juce::IPAddress> addrs = juce::IPAddress::getAllAddresses(false);
  juce::IPAddress loopback = juce::IPAddress::local(false);
  for (const auto& a : addrs) {
    if (a.isNull())
      continue;
    juce::String s = a.toString();
    if (s.isEmpty())
      continue;
    if (a == loopback)
      out.insert(0, s);
    else if (!out.contains(s))
      out.add(s);
  }
  if (out.isEmpty())
    out.add("127.0.0.1");
  return out;
}
