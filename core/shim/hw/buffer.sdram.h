/**
 * @file buffer.sdram.h
 * @brief Desktop stand-in for the firmware's src/hw/buffer.sdram.h.
 *
 * Same interface and sizes as the firmware's SDRAMBuffer, but the buffers
 * are on the heap, one pool per engine instead of one static pool in SDRAM.
 * pool() returns the pool of the engine currently running (Context.h).
 * About 32 MB: two 42 s stereo source buffers (the firmware's third, for
 * undo, is not used by v1.3.1), plus the delay and detector lines.
 *
 * The firmware's include, "hw/buffer.sdram.h", finds this file because the
 * shim directory comes first on the include path.
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Context.h"
#include "core/buffer.h"
#include "core/detector.h"
#include "core/event.h"
#include "core/fx/fx.h"
#include "core/track.h"
#include "nocopy.h"

namespace spotykach {

static constexpr size_t  kSampleRate           { 48000 };
static constexpr uint8_t kDecksCount           { 2 };
static constexpr uint8_t kBuffersPerDeckCount  { 1 };
static constexpr uint8_t kChannelsCount        { 2 };
static constexpr uint8_t kSourceMaxSeconds     { 42 };

class SDRAMBuffer {
public:
    static constexpr size_t kSourceBufferLength = kSourceMaxSeconds * kSampleRate;
    static constexpr size_t kSourceBufCount     = kDecksCount * kBuffersPerDeckCount;
    static constexpr size_t kLineBufCount       = kDecksCount * kBuffersPerDeckCount * kChannelsCount;

    /** The current engine's pool. */
    static SDRAMBuffer& pool() {
        auto* p = scotykach::shim::Context::Current().pool;
        assert(p != nullptr && "firmware called outside an engine scope");
        return *p;
    }

    /** Allocates and zeroes everything. Not real-time safe. */
    SDRAMBuffer();
    ~SDRAMBuffer() = default;

    Buffer::Frame* sourceBuffer();
    size_t sourceBufferSize();

    float* delayBuffer();

    float* detectorBuffer();

    Event* track_buffer_a() const;
    Event* track_buffer_b() const;

    size_t* slices_a() const;
    size_t* slices_b() const;

    /** Hands the buffers out again from the start, for a re-init. */
    void rewind();

private:
    NOCOPY(SDRAMBuffer)

    std::vector<Buffer::Frame> _source[kSourceBufCount];
    std::vector<float>         _delay[kLineBufCount];
    std::vector<float>         _detector[kLineBufCount];
    mutable std::vector<size_t> _slices[kDecksCount];
    mutable std::vector<Event>  _track[kDecksCount];

    size_t _providedSourceBufCount      { 0 };
    size_t _providedDetectorBufCount    { 0 };
    size_t _providedDelayBufCount       { 0 };
};
};
