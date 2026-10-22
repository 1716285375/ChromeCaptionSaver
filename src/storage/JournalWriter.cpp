#include "ChromeCaptionSaver/storage/JournalWriter.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <ctime>
#include <cstdio>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ccs::storage {

JournalWriter::~JournalWriter() { Close(); }

bool JournalWriter::Open(const std::string& dir_utf8) {
    Close();
    std::lock_guard lk(mtx_);
    out_.clear();
    std::error_code ec;
    fs::create_directories(fs::path(std::u8string(dir_utf8.begin(), dir_utf8.end())), ec);
    if (ec) {
        spdlog::error("create_directories failed: {}", ec.message());
        return false;
    }
    auto path = fs::path(std::u8string(dir_utf8.begin(), dir_utf8.end())) / "snapshots.jsonl";
    out_.open(path, std::ios::out | std::ios::app | std::ios::binary);
    if (!out_.is_open()) {
        spdlog::error("Failed to open {}", dir_utf8);
        return false;
    }
    spdlog::info("JournalWriter opened: {}", dir_utf8);
    return true;
}

void JournalWriter::Close() {
    std::lock_guard lk(mtx_);
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

bool JournalWriter::Append(const core::CaptionSnapshot& snap, uint64_t seq) {
    std::lock_guard lk(mtx_);
    if (!out_.is_open()) return false;

    json j;
    j["session_id"] = snap.session_id;
    j["source_id"] = snap.source_id;
    j["seq"] = seq;
    j["t_ms"] = snap.t_ms;

    // UTC ISO8601
    auto t = std::chrono::system_clock::to_time_t(snap.observed_at);
    std::tm tm_utc{};
    if (::gmtime_s(&tm_utc, &t) != 0) return false;
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    j["utc"] = buf;

    j["pid"] = snap.process_id;
    char hwnd_buf[32];
    std::snprintf(hwnd_buf, sizeof(hwnd_buf), "0x%llx", (unsigned long long)snap.hwnd);
    j["hwnd"] = hwnd_buf;
    j["method"] = snap.extraction_method;
    j["lines"] = snap.lines_utf8;
    j["joined"] = snap.joined_utf8;
    j["visible"] = snap.visible;

    try {
        const auto line = j.dump();
        out_ << line << "\n";
    } catch (const json::exception&) {
        spdlog::error("Snapshot contains invalid JSON text encoding");
        return false;
    }
    if (!out_) spdlog::error("Failed to append snapshot journal");
    return out_.good();
}

bool JournalWriter::Flush() {
    std::lock_guard lk(mtx_);
    if (!out_.is_open()) return false;
    out_.flush();
    if (!out_) spdlog::error("Failed to flush snapshot journal");
    return out_.good();
}

} // namespace ccs::storage
