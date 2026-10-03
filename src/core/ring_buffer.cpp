#include "ring_buffer.h"

namespace echopad {

SampleRing::SampleRing(size_t capacityFrames, uint16_t channels)
    : capacityFrames_(1), mask_(0), channels_(channels ? channels : 2) {
    size_t wanted = 1;
    while (wanted < capacityFrames) {
        wanted <<= 1;
    }
    capacityFrames_ = wanted;
    mask_ = wanted - 1;
    buffer_.assign(capacityFrames_ * channels_, 0.0f);
}

size_t SampleRing::FramesAvailable() const noexcept {
    const size_t write = writeFrame_.load(std::memory_order_acquire);
    const size_t read = readFrame_.load(std::memory_order_acquire);
    return write - read;
}

size_t SampleRing::Write(const Sample* interleaved, size_t frames) noexcept {
    if (!interleaved || frames == 0) {
        return 0;
    }
    const size_t write = writeFrame_.load(std::memory_order_relaxed);
    const size_t read = readFrame_.load(std::memory_order_acquire);
    const size_t free = capacityFrames_ - (write - read);
    if (free == 0) {
        return 0;
    }
    const size_t count = frames < free ? frames : free;

    // Never overwrite unread audio: on overflow we simply drop the tail of the
    // incoming block, which sounds like a dropout instead of a pitch glitch.
    const size_t offset = (write & mask_) * channels_;
    const size_t firstFrames = (count < capacityFrames_ - (write & mask_))
                                   ? count
                                   : capacityFrames_ - (write & mask_);
    std::memcpy(buffer_.data() + offset, interleaved, firstFrames * channels_ * sizeof(Sample));
    if (firstFrames < count) {
        std::memcpy(buffer_.data(), interleaved + firstFrames * channels_,
                    (count - firstFrames) * channels_ * sizeof(Sample));
    }

    writeFrame_.store(write + count, std::memory_order_release);
    return count;
}

size_t SampleRing::Read(Sample* interleaved, size_t frames) noexcept {
    if (!interleaved || frames == 0) {
        return 0;
    }
    const size_t read = readFrame_.load(std::memory_order_relaxed);
    const size_t write = writeFrame_.load(std::memory_order_acquire);
    const size_t available = write - read;
    if (available == 0) {
        return 0;
    }
    const size_t count = frames < available ? frames : available;

    const size_t offset = (read & mask_) * channels_;
    const size_t firstFrames = (count < capacityFrames_ - (read & mask_))
                                   ? count
                                   : capacityFrames_ - (read & mask_);
    std::memcpy(interleaved, buffer_.data() + offset, firstFrames * channels_ * sizeof(Sample));
    if (firstFrames < count) {
        std::memcpy(interleaved + firstFrames * channels_, buffer_.data(),
                    (count - firstFrames) * channels_ * sizeof(Sample));
    }

    readFrame_.store(read + count, std::memory_order_release);
    return count;
}

size_t SampleRing::ReadPadded(Sample* interleaved, size_t frames) noexcept {
    const size_t got = Read(interleaved, frames);
    if (got < frames) {
        std::memset(interleaved + got * channels_, 0, (frames - got) * channels_ * sizeof(Sample));
    }
    return got;
}

size_t SampleRing::DiscardOldest(size_t frames) noexcept {
    const size_t read = readFrame_.load(std::memory_order_relaxed);
    const size_t write = writeFrame_.load(std::memory_order_acquire);
    const size_t available = write - read;
    const size_t count = frames < available ? frames : available;
    if (count == 0) {
        return 0;
    }
    readFrame_.store(read + count, std::memory_order_release);
    return count;
}

void SampleRing::Clear() noexcept {    const size_t write = writeFrame_.load(std::memory_order_acquire);
    readFrame_.store(write, std::memory_order_release);
}

}  // namespace echopad
