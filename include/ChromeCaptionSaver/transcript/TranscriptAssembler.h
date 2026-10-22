#pragma once
#include "ChromeCaptionSaver/core/Types.h"

namespace ccs::transcript {
class TranscriptAssembler {
public:
    void Add(const core::CaptionSnapshot& snapshot, uint64_t seq);
    void Finish(int64_t end_ms);
    std::vector<core::CaptionSegment> Preview(int64_t end_ms) const;
    const std::vector<core::CaptionSegment>& segments() const { return segments_; }
private:
    void Commit(int64_t end_ms, const char* quality);
    std::vector<core::CaptionSegment> segments_;
    core::CaptionSegment pending_;
    int64_t last_ms_ = 0;
};
}
