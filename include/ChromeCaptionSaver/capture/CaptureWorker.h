#pragma once
// Background capture worker: runs on its own MTA thread, polls UIA at a fixed
// interval, and pushes CaptionSnapshot DTOs into a mutex-protected queue for
// the storage thread to write.
#include "ChromeCaptionSaver/core/Types.h"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace ccs::capture {

struct CaptureConfig {
    int      poll_interval_ms = 200;
    std::string session_id;
};

struct CaptureStats {
    std::atomic<uint64_t> snapshots_written{0}; // Queued snapshots; not a disk durability count.
    std::atomic<uint64_t> poll_cycles{0};
    std::atomic<uint64_t> changes_detected{0};
    std::atomic<core::CaptureState> state{core::CaptureState::Idle};
};

class CaptureWorker {
public:
    CaptureWorker();
    ~CaptureWorker();

    // Start capturing on the given HWND. Spawns the worker thread.
    bool Start(HWND target_hwnd, const CaptureConfig& cfg);
    void Pause();
    void Resume();
    void Stop();

    // Drain all pending snapshots (thread-safe; called by storage/main thread).
    bool Drain(std::vector<core::CaptionSnapshot>& out);

    const CaptureStats& stats() const { return stats_; }

private:
    void WorkerMain(std::stop_token st, HWND hwnd, std::chrono::steady_clock::time_point session_start);

    CaptureConfig cfg_;
    std::jthread  thread_;
    CaptureStats  stats_;

    std::mutex              mtx_;
    std::queue<core::CaptionSnapshot> queue_;
    std::mutex wait_mtx_;
    std::condition_variable_any wake_;
    std::wstring            last_text_;  // for change detection
};

} // namespace ccs::capture
