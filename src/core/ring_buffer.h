// EchoPad - lock-free single-producer/single-consumer primitives.
//
// The audio thread never blocks and never allocates. Control threads talk to it
// through these queues; the capture thread hands samples to it through the
// sample ring.
#pragma once

#include "common.h"

namespace echopad {

// Fixed-capacity SPSC queue of small POD items.
template <typename T, size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2, "capacity must be at least 2");

public:
    // Producer side. Returns false when the queue is full.
    bool Push(const T& item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) % Capacity;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        slots_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false when the queue is empty.
    bool Pop(T& out) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        out = slots_[tail];
        tail_.store((tail + 1) % Capacity, std::memory_order_release);
        return true;
    }

    bool Empty() const noexcept {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }

private:
    T slots_[Capacity]{};
    std::atomic<size_t> head_{0};  // written by the producer
    std::atomic<size_t> tail_{0};  // written by the consumer
};

// Power-of-two ring of float samples, used to move microphone audio from the
// capture thread to the audio thread. Lock-free, and tolerant of the two ends
// running at different rates: a read that finds too little data just returns
// what is there.
class SampleRing {
public:
    explicit SampleRing(size_t capacityFrames, uint16_t channels);

    // Producer. Returns the number of frames actually written (may be less
    // than requested when the ring is full, in which case the oldest audio is
    // not overwritten - dropping new input avoids audible pitch artifacts).
    size_t Write(const Sample* interleaved, size_t frames) noexcept;

    // Consumer. Reads up to `frames`; returns how many were produced.
    size_t Read(Sample* interleaved, size_t frames) noexcept;

    // Consumer. Reads `frames`, zero-filling any shortfall. Used to keep the
    // output timeline continuous when the capture side momentarily starves.
    size_t ReadPadded(Sample* interleaved, size_t frames) noexcept;

    size_t FramesAvailable() const noexcept;

    // Consumer. Drops up to `frames` of the oldest audio. Used to bound the
    // microphone latency when the capture and render clocks drift apart: rather
    // than letting the backlog grow without limit, we resync once.
    size_t DiscardOldest(size_t frames) noexcept;

    // Discard everything. Consumer side only.
    void Clear() noexcept;

    uint16_t channels() const noexcept { return channels_; }

private:
    std::vector<Sample> buffer_;
    size_t capacityFrames_;
    size_t mask_;
    uint16_t channels_;
    std::atomic<size_t> writeFrame_{0};
    std::atomic<size_t> readFrame_{0};
};

}  // namespace echopad
