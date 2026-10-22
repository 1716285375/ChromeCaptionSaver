#include "ChromeCaptionSaver/transcript/TranscriptAssembler.h"
#include <algorithm>
#include <stdexcept>

namespace ccs::transcript {
namespace {
bool Boundary(const std::string& text, size_t i) {
    return i == text.size() || (static_cast<unsigned char>(text[i]) & 0xc0) != 0x80;
}
size_t Characters(const std::string& text, size_t size) {
    size_t count = 0;
    for (size_t i = 0; i < size; ++i) if (Boundary(text, i)) ++count;
    return count;
}
std::string Normalize(const std::string& text) {
    std::string result;
    bool space = false;
    for (unsigned char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            space = !result.empty();
        } else {
            if (space) result += ' ';
            result += static_cast<char>(c);
            space = false;
        }
    }
    return result;
}
}
void TranscriptAssembler::Commit(int64_t end_ms, const char* quality) {
    if (pending_.text_utf8.empty()) return;
    pending_.id = segments_.size() + 1;
    pending_.end_ms = std::max(end_ms, pending_.start_ms + 1);
    pending_.quality = quality;
    segments_.push_back(std::move(pending_));
    pending_ = {};
}
void TranscriptAssembler::Add(const core::CaptionSnapshot& snapshot, uint64_t seq) {
    if (snapshot.t_ms < 0 || snapshot.t_ms < last_ms_)
        throw std::invalid_argument("Snapshot times must be nonnegative and ordered");
    last_ms_ = snapshot.t_ms;
    auto text = Normalize(snapshot.joined_utf8);
    if (!snapshot.visible || text.empty()) { Commit(snapshot.t_ms, "stable"); return; }
    if (!pending_.text_utf8.empty()) {
        const auto previous = pending_.text_utf8;
        if (text == previous) { pending_.last_snapshot_seq = seq; return; }
        if (text.starts_with(previous)) {
            pending_.text_utf8 = text;
            pending_.last_snapshot_seq = seq;
            return;
        }
        size_t overlap = 0;
        for (size_t n = std::min(previous.size(), text.size()); n > 0; --n) {
            if (Boundary(previous, previous.size() - n) && Boundary(text, n) &&
                previous.compare(previous.size() - n, n, text, 0, n) == 0 &&
                Characters(text, n) >= 2) { overlap = n; break; }
        }
        if (overlap) {
            pending_.text_utf8 = Normalize(previous.substr(0, previous.size() - overlap));
            Commit(snapshot.t_ms, "stable");
        } else {
            size_t common = 0;
            while (common < previous.size() && common < text.size() && previous[common] == text[common]) ++common;
            while (common && !Boundary(text, common)) --common;
            // shortcut: shared-prefix revisions are heuristic; raw JSONL allows later reassembly.
            if ((text.size() < previous.size() && previous.starts_with(text)) ||
                (Characters(text, common) >= 3 && common * 2 >= std::min(previous.size(), text.size()))) {
                pending_.text_utf8 = text;
                pending_.last_snapshot_seq = seq;
                pending_.quality = "tentative";
                return;
            }
            Commit(snapshot.t_ms, "ambiguous");
        }
    }
    pending_.start_ms = segments_.empty() ? snapshot.t_ms : std::max(snapshot.t_ms, segments_.back().end_ms);
    pending_.text_utf8 = std::move(text);
    pending_.first_snapshot_seq = pending_.last_snapshot_seq = seq;
    pending_.quality = "tentative";
}
void TranscriptAssembler::Finish(int64_t end_ms) {
    Commit(std::max(last_ms_, end_ms), "tentative");
}
std::vector<core::CaptionSegment> TranscriptAssembler::Preview(int64_t end_ms) const {
    auto copy = *this;
    copy.Finish(end_ms);
    return copy.segments_;
}
}
