/*
  ==============================================================================
    DeferredDeleter.h
    Lock-Free: Uses juce::AbstractFifo. No mutex on audio thread.
  ==============================================================================
*/

#pragma once

#include "../Core/TimerHub.h"
#include <array>
#include <functional>
#include <juce_core/juce_core.h>
#include <memory>

class DeferredDeleter {
public:
  DeferredDeleter() {
    hubId = "DeferredDeleter_" + juce::Uuid().toDashedString().toStdString();
    TimerHub::instance().subscribe(hubId, [this] { cleanup(); },
                                   TimerHub::Rate10Hz);
  }

  ~DeferredDeleter() {
    TimerHub::instance().unsubscribe(hubId);
    cleanup();
  }

  // Thread-Safe, Lock-Free: Schedule raw pointer for deletion
  template <typename T>
  void deleteAsync(T *object) {
    if (!object)
      return;

    int start1, size1, start2, size2;
    fifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0) {
      ptrBuffer[start1] = object;
      deleterBuffer[start1] = [](void *ptr) { delete static_cast<T *>(ptr); };
      fifo.finishedWrite(1);
    }
  }

  // Thread-Safe, Lock-Free: Schedule shared_ptr for deletion
  template <typename T>
  void deleteAsync(std::shared_ptr<T> object) {
    if (!object)
      return;

    int start1, size1, start2, size2;
    fifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0) {
      auto *heapPtr = new std::shared_ptr<T>(std::move(object));
      ptrBuffer[start1] = heapPtr;
      deleterBuffer[start1] = [](void *ptr) {
        delete static_cast<std::shared_ptr<T> *>(ptr);
      };
      fifo.finishedWrite(1);
    }
  }

private:
  void cleanup() {
    int start1, size1, start2, size2;
    fifo.prepareToRead(fifo.getNumReady(), start1, size1, start2, size2);

    auto process = [this](int idx) {
      if (deleterBuffer[idx] && ptrBuffer[idx]) {
        deleterBuffer[idx](ptrBuffer[idx]);
        ptrBuffer[idx] = nullptr;
        deleterBuffer[idx] = nullptr;
      }
    };

    if (size1 > 0)
      for (int i = 0; i < size1; ++i)
        process(start1 + i);
    if (size2 > 0)
      for (int i = 0; i < size2; ++i)
        process(start2 + i);

    fifo.finishedRead(size1 + size2);
  }

  static constexpr int Capacity = 1024;
  std::string hubId;
  juce::AbstractFifo fifo{Capacity};
  std::array<void *, Capacity> ptrBuffer{};
  std::array<void (*)(void *), Capacity> deleterBuffer{};
};
