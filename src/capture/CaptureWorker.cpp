#include "ChromeCaptionSaver/capture/CaptureWorker.h"
#include "ChromeCaptionSaver/platform/UiaClient.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <windows.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace ccs::capture {

using ccs::core::CaptionSnapshot;
using ccs::core::CaptureState;

CaptureWorker::CaptureWorker() = default;
CaptureWorker::~CaptureWorker() { Stop(); }

bool CaptureWorker::Start(HWND hwnd, const CaptureConfig& cfg) {
    if (thread_.joinable() || !::IsWindow(hwnd) || cfg.poll_interval_ms <= 0) return false;
    {
        std::lock_guard lk(mtx_);
        queue_ = {};
    }
    last_text_.clear();
    stats_.snapshots_written = 0;
    stats_.poll_cycles = 0;
    stats_.changes_detected = 0;
    cfg_ = cfg;
    stats_.state = CaptureState::Discovering;
    const auto start = std::chrono::steady_clock::now();
    thread_ = std::jthread([this, hwnd, start](std::stop_token st) { WorkerMain(st, hwnd, start); });
    return true;
}

void CaptureWorker::Pause() {
    std::lock_guard lock(mtx_);
    auto expected = CaptureState::Capturing;
    stats_.state.compare_exchange_strong(expected, CaptureState::Paused);
}

void CaptureWorker::Resume() {
    {
        std::lock_guard lk(wait_mtx_);
        auto expected = CaptureState::Paused;
        stats_.state.compare_exchange_strong(expected, CaptureState::Capturing);
    }
    wake_.notify_all();
}

void CaptureWorker::Stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    // Preserve terminal failure so a caller joining the thread cannot miss it.
    if (stats_.state != CaptureState::TargetNotFound) stats_.state = CaptureState::Stopped;
}

bool CaptureWorker::Drain(std::vector<CaptionSnapshot>& out) {
    std::lock_guard lk(mtx_);
    if (queue_.empty()) return false;
    while (!queue_.empty()) {
        out.push_back(std::move(queue_.front()));
        queue_.pop();
    }
    return true;
}

static std::wstring JoinLines(const std::vector<std::wstring>& lines) {
    std::wstring out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += L"\n";
        out += lines[i];
    }
    return out;
}

static std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

void CaptureWorker::WorkerMain(std::stop_token st, HWND hwnd, std::chrono::steady_clock::time_point session_start) {
    spdlog::info("CaptureWorker thread started, hwnd=0x{:x}", reinterpret_cast<uintptr_t>(hwnd));

    ccs::platform::UiaClient uia;
    if (!uia.Initialize()) {
        stats_.state = CaptureState::TargetNotFound;
        spdlog::error("UIA init failed, aborting capture");
        return;
    }

    auto win_el = uia.ElementFromHandle(hwnd);
    if (!win_el) {
        stats_.state = CaptureState::TargetNotFound;
        spdlog::error("ElementFromHandle failed, aborting");
        return;
    }

    stats_.state = CaptureState::Capturing;

    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    while (!st.stop_requested()) {
        auto state = stats_.state.load();
        if (state == CaptureState::Paused) {
            std::unique_lock lk(wait_mtx_);
            wake_.wait(lk, st, [this] { return stats_.state != CaptureState::Paused; });
            continue;
        }
        if (state == CaptureState::Stopped || state == CaptureState::Finalizing) break;

        if (!::IsWindow(hwnd)) {
            stats_.state = CaptureState::TargetNotFound;
            return;
        }
        auto t0 = std::chrono::steady_clock::now();
        stats_.poll_cycles++;

        std::vector<std::wstring> lines;
        std::wstring method;
        bool ok = uia.ReadCaptionText(win_el.Get(), lines, method);
        if (!ok) {
            stats_.state = CaptureState::TargetNotFound;
            return;
        }

        auto now_wall = ccs::core::Clock::now();
        auto t_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - session_start).count();

        std::wstring joined = JoinLines(lines);

        // Change detection: only push snapshot if text changed.
        bool changed = (joined != last_text_);
        if (changed) {
            CaptionSnapshot snap;
            snap.session_id = cfg_.session_id;
            snap.observed_at = now_wall;
            snap.t_ms = t_ms;
            snap.process_id = pid;
            snap.hwnd = reinterpret_cast<uint64_t>(hwnd);
            snap.source_id = std::to_string(pid) + ":" + std::to_string(snap.hwnd);
            snap.extraction_method = WideToUtf8(method);
            for (auto& l : lines) snap.lines_utf8.push_back(WideToUtf8(l));
            snap.joined_utf8 = WideToUtf8(joined);
            snap.visible = ok;


            {
                std::lock_guard lk(mtx_);
                if (stats_.state == CaptureState::Paused) continue;
                queue_.push(std::move(snap));
                stats_.changes_detected++;
            }
            stats_.snapshots_written++;
            last_text_ = joined;
        }

        // Sleep for the remainder of the poll interval, interruptible by stop.
        auto elapsed = std::chrono::steady_clock::now() - t0;
        auto sleep_for = std::chrono::milliseconds(cfg_.poll_interval_ms) - elapsed;
        if (sleep_for.count() > 0) {
            if (st.stop_requested()) break;
            std::unique_lock lk(wait_mtx_);
            wake_.wait_for(lk, st, sleep_for, [] { return false; });
        }
    }

    stats_.state = CaptureState::Stopped;
    spdlog::info("CaptureWorker stopped, cycles={}, changes={}",
                 (uint64_t)stats_.poll_cycles, (uint64_t)stats_.changes_detected);
}

} // namespace ccs::capture
