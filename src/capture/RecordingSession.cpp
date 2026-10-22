#include "ChromeCaptionSaver/capture/RecordingSession.h"
#include "ChromeCaptionSaver/storage/JournalWriter.h"
#include "ChromeCaptionSaver/transcript/TranscriptAssembler.h"
#include "ChromeCaptionSaver/export/SubtitleExporter.h"
#include "ChromeCaptionSaver/platform/UiaClient.h"
#include "ChromeCaptionSaver/platform/WindowLocator.h"
#include <stdexcept>
#include "ChromeCaptionSaver/core/ChatQueue.h"

namespace ccs::capture {
namespace fs = std::filesystem;
static std::string UniqueName() {
    auto t = core::Clock::to_time_t(core::Clock::now());
    std::tm tm_local;
    ::localtime_s(&tm_local, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm_local);
    return std::string(buf) + "-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(core::Clock::now().time_since_epoch()).count() % 1000000) + "-" + std::to_string(GetCurrentProcessId());
}
RecordingSession::~RecordingSession() { Stop(); if (thread_.joinable()) thread_.join(); }
void RecordingSession::Stop() { if (thread_.joinable()) thread_.request_stop(); }
SessionView RecordingSession::View() const { std::lock_guard lock(mutex_); return view_; }
void RecordingSession::UpdateView(SessionView& cached) const {
    std::lock_guard lock(mutex_);
    if(cached.text_revision!=view_.text_revision || cached.directory!=view_.directory)cached=view_;
    else {cached.state=view_.state;cached.running=view_.running;cached.snapshots=view_.snapshots;cached.elapsed_ms=view_.elapsed_ms;cached.message=view_.message;cached.last_export=view_.last_export;}
}
std::vector<std::string> RecordingSession::DrainSentences() {
    std::lock_guard lock(mutex_);
    std::vector<std::string> result;
    while (!sentences_.empty()) { result.push_back(std::move(sentences_.front())); sentences_.pop_front(); }
    return result;
}
bool RecordingSession::Start(HWND target, const fs::path& base) {
    if (View().running) return false;
    if (thread_.joinable()) thread_.join();
    paused_ = false;
    export_requested_ = false;
    { std::lock_guard lock(mutex_); view_ = {}; sentences_.clear(); view_.running = true; view_.state = core::CaptureState::Discovering; }
    thread_ = std::jthread([this, target, base](std::stop_token stop) { Run(stop, target, base); });
    return true;
}
void RecordingSession::Run(std::stop_token stop, HWND target, fs::path base) {
    CaptureWorker worker;
    storage::JournalWriter writer;
    transcript::TranscriptAssembler assembler;
    uint64_t sequence = 0;
    chat::SentenceGate sentence_gate;
    auto start = std::chrono::steady_clock::now();
    fs::path directory;
    std::string latest;
    std::string stable_document, stable_current, stable_previous;
    uint64_t stable_sentences=0;
    auto elapsed = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(); };
    auto update = [&](core::CaptureState state, bool finish = false) {
        auto preview = assembler.Preview(elapsed());
        std::string display, stream, candidate;
        for (const auto& segment : preview) {
            if (!display.empty()) display += '\n';
            display += segment.text_utf8;
            if(segment.quality=="tentative")candidate=segment.text_utf8;
            if (!stream.empty() && !segment.text_utf8.empty() &&
                static_cast<unsigned char>(stream.back()) < 128 && static_cast<unsigned char>(segment.text_utf8.front()) < 128) stream += ' ';
            stream += segment.text_utf8;
        }
        auto ready = sentence_gate.Update(stream, elapsed(), finish);
        for(const auto& sentence:ready){
            if(!stable_document.empty())stable_document+='\n';stable_document+=sentence;
            stable_previous=stable_current;stable_current=sentence;++stable_sentences;
        }
        if(!stable_current.empty()){auto at=latest.rfind(stable_current);if(at!=std::string::npos)candidate=latest.substr(at+stable_current.size());}
        if(finish)candidate.clear();
        if (preview.size() > 200) preview.erase(preview.begin(), preview.end() - 200);
        std::lock_guard lock(mutex_);
        view_.state = state;
        view_.snapshots = sequence;
        view_.elapsed_ms = elapsed();
        const bool text_changed=view_.transcript_text!=display || view_.latest_text!=latest || view_.stable_text!=stable_document || view_.candidate_text!=candidate;
        view_.latest_text = latest;
        view_.transcript_text = std::move(display);
        if(text_changed)++view_.text_revision;
        view_.stable_text=stable_document;view_.stable_latest=stable_current;view_.stable_previous=stable_previous;view_.candidate_text=std::move(candidate);view_.stable_count=stable_sentences;
        for (auto& text : ready) sentences_.push_back(std::move(text));
        view_.preview = std::move(preview);
    };
    auto drain = [&] {
        std::vector<core::CaptionSnapshot> snapshots;
        worker.Drain(snapshots);
        for (const auto& snapshot : snapshots) {
            if (!writer.Append(snapshot, ++sequence)) throw std::runtime_error("原始字幕写入失败，已停止录制。");
            assembler.Add(snapshot, sequence);
            latest = snapshot.joined_utf8;
        }
        if (!snapshots.empty() && !writer.Flush()) throw std::runtime_error("原始字幕刷新失败，已停止录制。");
    };
    auto export_now = [&] {
        auto segments = assembler.Preview(elapsed());
        if (segments.empty()) {
            std::lock_guard lock(mutex_); view_.message = "还没有字幕文字，暂时无法导出。"; return;
        }
        const auto output = directory / "exports" / UniqueName();
        exporter::ExportBundle(segments, output);
        std::lock_guard lock(mutex_);
        view_.last_export = output;
        view_.message = "已导出 TXT、SRT、VTT 和分段日志。";
    };
    try {
        if (base.empty()) throw std::runtime_error("请选择保存目录。");
        platform::UiaClient uia;
        if (!uia.Initialize()) throw std::runtime_error("无法初始化 Windows UI Automation。");
        auto valid = [&](HWND hwnd) {
            auto element = uia.ElementFromHandle(hwnd);
            std::vector<std::wstring> lines;
            std::wstring method;
            return element && uia.ReadCaptionText(element.Get(), lines, method);
        };
        if (!target) {
            for (const auto& window : platform::FindChromeCaptionWindows()) {
                if (stop.stop_requested()) break;
                if (valid(window.hwnd)) { target = window.hwnd; break; }
            }
        }
        if (stop.stop_requested()) throw std::runtime_error("已取消开始录制。");
        if (!target || !valid(target)) throw std::runtime_error("未找到字幕控件。请开启 Chrome 实时字幕并播放音频。");
        const auto id = UniqueName();
        directory = base / id;
        auto utf8 = directory.u8string();
        if (!writer.Open(std::string(utf8.begin(), utf8.end()))) throw std::runtime_error("无法创建录制目录，请检查路径和写入权限。");
        { std::lock_guard lock(mutex_); view_.directory = directory; view_.message = "原始字幕正在自动保存。"; }
        CaptureConfig cfg;
        cfg.session_id = id;
        start = std::chrono::steady_clock::now();
        if (!worker.Start(target, cfg)) throw std::runtime_error("无法启动字幕采集。");
        bool was_paused = false;
        while (!stop.stop_requested()) {
            const bool pause = paused_.load();
            if (pause) worker.Pause(); else worker.Resume();
            drain();
            if (pause && !was_paused && worker.stats().state == core::CaptureState::Paused) {
                core::CaptionSnapshot boundary;
                boundary.session_id = id;
                boundary.observed_at = core::Clock::now();
                boundary.t_ms = elapsed();
                boundary.hwnd = reinterpret_cast<uint64_t>(target);
                DWORD pid = 0; GetWindowThreadProcessId(target, &pid);
                boundary.process_id = pid;
                boundary.source_id = std::to_string(pid) + ":" + std::to_string(boundary.hwnd);
                boundary.extraction_method = "PauseBoundary";
                boundary.visible = false;
                if (!writer.Append(boundary, ++sequence) || !writer.Flush()) throw std::runtime_error("暂停边界保存失败。");
                assembler.Add(boundary, sequence);
            }
            was_paused = pause;
            const auto state = worker.stats().state.load();
            update(state);
            if (export_requested_.exchange(false)) {
                try { export_now(); }
                catch (const std::exception&) { std::lock_guard lock(mutex_); view_.message = "导出失败，请检查剩余空间；原始快照继续保存。"; }
            }
            if (state == core::CaptureState::TargetNotFound) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        update(core::CaptureState::Finalizing);
        worker.Stop();
        drain();
        const bool lost = worker.stats().state == core::CaptureState::TargetNotFound;
        if (!writer.Flush()) throw std::runtime_error("保存最后一批原始字幕失败。");
        writer.Close();
        export_now();
        update(lost ? core::CaptureState::TargetNotFound : core::CaptureState::Stopped, true);
        std::lock_guard lock(mutex_);
        view_.running = false;
        if (lost) view_.message = "字幕控件已断开，现有录制与导出已保留。";
    } catch (const std::exception& e) {
        worker.Stop();
        writer.Close();
        update(core::CaptureState::Stopped);
        std::lock_guard lock(mutex_);
        view_.running = false;
        view_.message = e.what();
    }
}
}
