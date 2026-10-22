#pragma once
#include "ChromeCaptionSaver/capture/CaptureWorker.h"
#include <filesystem>
#include <deque>

namespace ccs::capture {
struct SessionView {
    core::CaptureState state = core::CaptureState::Idle;
    bool running = false;
    uint64_t snapshots = 0;
    int64_t elapsed_ms = 0;
    std::string latest_text, transcript_text, message;
    std::string stable_text, stable_latest, stable_previous, candidate_text;
    uint64_t text_revision = 0, stable_count = 0;
    std::filesystem::path directory, last_export;
    std::vector<core::CaptionSegment> preview;
};
// Coordinates capture, journal persistence, assembly and exports on a background thread.
class RecordingSession {
public:
    ~RecordingSession();
    bool Start(HWND target, const std::filesystem::path& base_directory);
    void Pause(bool paused) { paused_ = paused; }
    void Stop();
    void Export() { export_requested_ = true; }
    SessionView View() const;
    void UpdateView(SessionView& cached) const;
    std::vector<std::string> DrainSentences();
private:
    void Run(std::stop_token stop, HWND target, std::filesystem::path base);
    mutable std::mutex mutex_;
    SessionView view_;
    std::deque<std::string> sentences_;
    std::jthread thread_;
    std::atomic<bool> paused_{false}, export_requested_{false};
};
}
