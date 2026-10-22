#include "ChromeCaptionSaver/export/SubtitleExporter.h"
#include "ChromeCaptionSaver/transcript/TranscriptAssembler.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>
#include <limits>

namespace ccs::exporter {
namespace fs = std::filesystem;
using json = nlohmann::json;
std::string Timestamp(int64_t ms, char separator) {
    if (ms < 0) throw std::invalid_argument("Negative subtitle timestamp");
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << ms / 3600000 << ':'
        << std::setw(2) << ms / 60000 % 60 << ':' << std::setw(2) << ms / 1000 % 60
        << separator << std::setw(3) << ms % 1000;
    return out.str();
}
static std::string Escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '\n' || c == '\r') out += ' ';
        else out += c;
    }
    return out;
}
void ExportBundle(const std::vector<core::CaptionSegment>& segments, const fs::path& directory) {
    if (segments.empty()) throw std::invalid_argument("No caption text to export");
    int64_t previous_end = 0;
    for (const auto& s : segments) {
        if (s.start_ms < previous_end || s.end_ms <= s.start_ms || s.text_utf8.empty())
            throw std::invalid_argument("Invalid or overlapping subtitle segment");
        (void)json(s.text_utf8).dump(); // Validate UTF-8 before creating any files.
        previous_end = s.end_ms;
    }
    if (!directory.parent_path().empty()) fs::create_directories(directory.parent_path());
    if (!fs::create_directory(directory)) throw std::runtime_error("Export directory already exists; choose a new directory");
    std::ostringstream txt, srt, vtt, raw;
    vtt << "WEBVTT\n\nNOTE Times are approximate capture times, not video timestamps.\n\n";
    size_t cue_id = 0;
    for (const auto& s : segments) {
        txt << s.text_utf8 << '\n';
        raw << json{{"id", s.id}, {"start_ms", s.start_ms}, {"end_ms", s.end_ms},
            {"text", s.text_utf8}, {"quality", s.quality},
            {"first_snapshot_seq", s.first_snapshot_seq}, {"last_snapshot_seq", s.last_snapshot_seq}}.dump() << '\n';
        // Split large bubbles on UTF-8 boundaries; distribute their approximate duration.
        std::vector<std::string> chunks;
        size_t start = 0, chars = 0;
        for (size_t i = 0; i < s.text_utf8.size(); ++i) {
            if ((static_cast<unsigned char>(s.text_utf8[i]) & 0xc0) == 0x80) continue;
            if (chars == 84) { chunks.push_back(s.text_utf8.substr(start, i - start)); start = i; chars = 0; }
            ++chars;
        }
        chunks.push_back(s.text_utf8.substr(start));
        // Very short segments must still have at least one millisecond per cue.
        if (s.end_ms - s.start_ms < static_cast<int64_t>(chunks.size())) chunks = {s.text_utf8};
        const auto count = static_cast<int64_t>(chunks.size());
        for (int64_t i = 0; i < count; ++i) {
            const auto duration = s.end_ms - s.start_ms;
            const auto begin = s.start_ms + duration / count * i + duration % count * i / count;
            const auto end = s.start_ms + duration / count * (i + 1) + duration % count * (i + 1) / count;
            srt << ++cue_id << '\n' << Timestamp(begin, ',') << " --> " << Timestamp(end, ',') << '\n'
                << Escape(chunks[static_cast<size_t>(i)]) << "\n\n";
            vtt << Timestamp(begin, '.') << " --> " << Timestamp(end, '.') << '\n'
                << Escape(chunks[static_cast<size_t>(i)]) << "\n\n";
        }
    }
    for (const auto& [name, content] : std::vector<std::pair<std::string, std::string>>{
        {"transcript.txt", txt.str()}, {"segments.jsonl", raw.str()},
        {"captions.approx.srt", srt.str()}, {"captions.approx.vtt", vtt.str()}}) {
        const auto temp = directory / (name + ".part");
        std::ofstream out(temp, std::ios::binary);
        out << content;
        out.flush();
        if (!out) throw std::runtime_error("Failed to write export file: " + name);
        out.close();
        if (!out) throw std::runtime_error("Failed to close export file: " + name);
        fs::rename(temp, directory / name);
    }
}
std::vector<core::CaptionSegment> ReadJournal(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open snapshot journal");
    transcript::TranscriptAssembler assembler;
    std::vector<core::CaptionSegment> result;
    std::string line, session;
    int64_t offset = 0, last = 0;
    uint64_t sequence = 0;
    size_t number = 0;
    auto finish = [&] {
        assembler.Finish(last + 1000);
        for (auto segment : assembler.segments()) {
            segment.id = result.size() + 1;
            result.push_back(std::move(segment));
        }
        assembler = {};
    };
    while (std::getline(input, line)) {
        ++number;
        try {
            const auto j = json::parse(line);
            core::CaptionSnapshot snapshot;
            const auto id = j.at("session_id").get<std::string>();
            if (!j.at("t_ms").is_number_integer() || !j.at("seq").is_number_unsigned())
                throw std::runtime_error("Timestamp and sequence must be integers; sequence must be positive");
            auto ms = j.at("t_ms").get<int64_t>();
            if (ms < 0 || ms > 31536000000LL) throw std::runtime_error("Timestamp outside supported one-year session range");
            if (number > 1 && id != session) {
                finish();
                offset = result.empty() ? last + 1000 : result.back().end_ms;
                sequence = 0;
            }
            session = id;
            const auto seq = j.at("seq").get<uint64_t>();
            if (seq <= sequence) throw std::runtime_error("Snapshot sequence is not increasing");
            sequence = seq;
            snapshot.t_ms = offset + ms;
            snapshot.joined_utf8 = j.at("joined").get<std::string>();
            snapshot.visible = j.value("visible", true);
            assembler.Add(snapshot, seq);
            last = snapshot.t_ms;
        } catch (const std::exception& e) {
            throw std::runtime_error("Journal line " + std::to_string(number) + ": " + e.what());
        }
    }
    if (input.bad()) throw std::runtime_error("Failed to read snapshot journal");
    finish();
    return result;
}
}
