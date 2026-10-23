#include "hw/buffer.sdram.h"

using namespace spotykach;

SDRAMBuffer::SDRAMBuffer()
{
    for (auto& b : _source)   b.assign(kSourceBufferLength, Buffer::Frame { 0.f, 0.f });
    for (auto& b : _delay)    b.assign(Fx::kEchoDelayBufferLength, 0.f);
    for (auto& b : _detector) b.assign(Detector::kWindow, 0.f);
    for (auto& b : _slices)   b.assign(kMaxSlicePointCount, 0);
    for (auto& b : _track)    b.assign(Track::kLength, Event {});
}

Buffer::Frame* SDRAMBuffer::sourceBuffer()
{
    assert(_providedSourceBufCount < kSourceBufCount);
    return _source[_providedSourceBufCount++].data();
}

size_t SDRAMBuffer::sourceBufferSize()
{
    return kSourceBufferLength;
}

float* SDRAMBuffer::detectorBuffer()
{
    assert(_providedDetectorBufCount < kLineBufCount);
    return _detector[_providedDetectorBufCount++].data();
}

float* SDRAMBuffer::delayBuffer()
{
    assert(_providedDelayBufCount < kLineBufCount);
    return _delay[_providedDelayBufCount++].data();
}

Event* SDRAMBuffer::track_buffer_a() const { return _track[0].data(); }
Event* SDRAMBuffer::track_buffer_b() const { return _track[1].data(); }

size_t* SDRAMBuffer::slices_a() const { return _slices[0].data(); }
size_t* SDRAMBuffer::slices_b() const { return _slices[1].data(); }

void SDRAMBuffer::rewind()
{
    _providedSourceBufCount = _providedDetectorBufCount = _providedDelayBufCount = 0;
}
