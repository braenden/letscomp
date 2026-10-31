#pragma once

#include "ScopeData.h"
#include <algorithm>
#include <vector>

namespace letscomp::visualization
{
class HistoryBuffer
{
public:
    explicit HistoryBuffer(size_t capacity = 4000) : frames(capacity) {}

    void push(const ScopeFrame& frame) noexcept
    {
        if (frames.empty()) return;
        frames[writeIndex] = frame;
        writeIndex = (writeIndex + 1) % frames.size();
        validFrames = std::min(validFrames + 1, frames.size());
    }

    void clear() noexcept
    {
        std::fill(frames.begin(), frames.end(), ScopeFrame{});
        writeIndex = validFrames = 0;
    }

    size_t size() const noexcept { return validFrames; }
    size_t capacity() const noexcept { return frames.size(); }

    const ScopeFrame& at(size_t chronologicalIndex) const noexcept
    {
        const auto oldest = validFrames < frames.size() ? 0 : writeIndex;
        return frames[(oldest + chronologicalIndex) % frames.size()];
    }

private:
    std::vector<ScopeFrame> frames;
    size_t writeIndex = 0, validFrames = 0;
};
}

