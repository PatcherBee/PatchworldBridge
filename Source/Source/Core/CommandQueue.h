/* Source/Engine/CommandQueue.h */
#pragma once
#include <juce_core/juce_core.h>

struct BridgeCommand {
  enum Type {
    Panic,
    Transport,           // value > 0.5 = Play, <= 0.5 = Stop
    Reset,               // Reset transport
    SetBpm,              // value = new BPM
    SetScaleQuantization // value > 0.5 = Enabled
  };
  Type type;
  float value = 0.0f;
  int intValue = 0;
};

/**
 * Single-Producer Single-Consumer lock-free command queue
 */
class CommandQueue {
public:
  CommandQueue(int capacity = 64) : fifo(capacity) { buffer.resize(capacity); }

  bool push(const BridgeCommand &cmd) {
    int start1, size1, start2, size2;
    fifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0) {
      buffer[start1] = cmd;
      fifo.finishedWrite(1);
      return true;
    }
    return false;
  }

  bool pop(BridgeCommand &cmd) {
    int start1, size1, start2, size2;
    fifo.prepareToRead(1, start1, size1, start2, size2);

    if (size1 > 0) {
      cmd = buffer[start1];
      fifo.finishedRead(1);
      return true;
    }
    return false;
  }

private:
  juce::AbstractFifo fifo;
  std::vector<BridgeCommand> buffer;
};
