#pragma once
// ChromeCaptionSaver — core data types (no platform-specific includes)
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>

namespace ccs::core {

// Wall-clock timestamp (UTC)
using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;

// Monotonic clock offset from session start (ms)
using MonoMs = std::chrono::milliseconds;

struct CaptionSnapshot {
    std::string session_id;
    TimePoint   observed_at;       // wall clock (UTC)
    int64_t     t_ms = 0;         // ms since session start (monotonic)
    uint32_t    process_id = 0;
    uint64_t    hwnd = 0;
    std::string source_id;
    std::string extraction_method; // "TextPattern" | "StaticText"
    std::vector<std::string> lines_utf8;
    std::string joined_utf8;
    bool        visible = true;
};

struct CaptionSegment {
    uint64_t    id = 0;
    int64_t     start_ms = 0;
    int64_t     end_ms = 0;
    std::string text_utf8;
    std::string quality;          // "stable" | "tentative" | "ambiguous"
    uint64_t    first_snapshot_seq = 0;
    uint64_t    last_snapshot_seq = 0;
};

enum class CaptureState {
    Idle,
    Discovering,
    Capturing,
    Paused,
    Reconnecting,
    TargetNotFound,
    Finalizing,
    Stopped,
};

inline const char* ToString(CaptureState s) {
    switch (s) {
        case CaptureState::Idle:           return "Idle";
        case CaptureState::Discovering:    return "Discovering";
        case CaptureState::Capturing:      return "Capturing";
        case CaptureState::Paused:         return "Paused";
        case CaptureState::Reconnecting:   return "Reconnecting";
        case CaptureState::TargetNotFound: return "TargetNotFound";
        case CaptureState::Finalizing:     return "Finalizing";
        case CaptureState::Stopped:        return "Stopped";
    }
    return "Unknown";
}

} // namespace ccs
