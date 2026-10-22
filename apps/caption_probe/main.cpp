// caption_probe — diagnostic CLI for ChromeCaptionSaver
// Usage: see --help
#include "ChromeCaptionSaver/platform/WindowLocator.h"
#include "ChromeCaptionSaver/platform/UiaClient.h"
#include "ChromeCaptionSaver/capture/CaptureWorker.h"
#include "ChromeCaptionSaver/storage/JournalWriter.h"
#include "ChromeCaptionSaver/core/Types.h"
#include "ChromeCaptionSaver/core/Paths.h"
#include "ChromeCaptionSaver/export/SubtitleExporter.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <cwctype>
#include <set>
#include <atomic>

using json = nlohmann::json;

namespace {

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::atomic<bool> interrupted{false};

BOOL WINAPI ConsoleHandler(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    interrupted.store(true);
    return TRUE;
}

HWND FindCaptionWindow() {
    ccs::platform::UiaClient uia;
    if (!uia.Initialize()) return nullptr;
    for (const auto& window : ccs::platform::FindChromeCaptionWindows()) {
        auto element = uia.ElementFromHandle(window.hwnd);
        std::vector<std::wstring> lines;
        std::wstring method;
        if (element && uia.ReadCaptionText(element.Get(), lines, method)) return window.hwnd;
    }
    return nullptr;
}

void PrintHelp() {
    std::cout <<
R"(ChromeCaptionSaver caption_probe v0.2.0

USAGE:
  caption_probe --export JOURNAL [--outdir NEW_DIRECTORY]
  caption_probe --version
  caption_probe --help
  caption_probe --list-windows
  caption_probe --probe [--duration SECONDS] [--hwnd HWND]
  caption_probe --dump-tree --hwnd HWND [--depth N] [--output FILE] [--include-text]
  caption_probe --select-point
  caption_probe --record [--hwnd HWND] [--duration SECONDS] [--outdir DIR]

OPTIONS:
  --hwnd HWND       Window handle in hex, e.g. 0x00000000000A0B12
  --duration SEC    Probe/record duration, 1..86400 seconds (default 10)
  --depth N         UIA tree depth for dump, 1..64 (default 8)
  --output FILE     Write dump JSON to FILE instead of stdout
  --include-text    Include actual caption text in dump (default redacted)
  --outdir DIR      Output directory for snapshots.jsonl (default project data/sessions/<timestamp>)
)";
}

int CmdListWindows() {
    auto wins = ccs::platform::FindChromeCaptionWindows();
    std::cout << "Found " << wins.size() << " Chrome top-level candidate window(s):\n";
    for (size_t i = 0; i < wins.size(); ++i) {
        const auto& w = wins[i];
        std::cout << "[" << i << "] hwnd=0x" << std::hex << (uintptr_t)w.hwnd << std::dec
                   << " pid=" << w.pid
                   << " score=" << w.score
                   << " visible=" << (w.visible ? "yes" : "no")
                   << " rect=(" << w.rect.left << "," << w.rect.top
                   << "," << w.rect.right << "," << w.rect.bottom << ")\n"
                   << "    class=" << WideToUtf8(w.class_name) << "\n"
                   << "    title =" << WideToUtf8(w.title) << "\n"
                   << "    exe   =" << WideToUtf8(w.exe_path) << "\n"
                   << "    why   =" << WideToUtf8(w.score_reason) << "\n";
    }
    return 0;
}

int CmdProbe(int duration_sec, HWND hwnd_opt) {
    ccs::platform::UiaClient uia;
    if (!uia.Initialize()) {
        std::cerr << "UIA init failed, hr=" << std::hex << uia.LastHr() << "\n";
        return 1;
    }

    HWND target = hwnd_opt;
    if (!target) {
        target = FindCaptionWindow();
        if (!target) {
            std::cerr << "No Chrome caption window found. Open Live Caption in Chrome and retry,\n"
                       << "or pass --hwnd explicitly.\n";
            return 2;
        }
        std::cout << "Auto-selected best candidate: hwnd=0x" << std::hex << (uintptr_t)target << std::dec
                   << "\n";
    }

    auto win_el = uia.ElementFromHandle(target);
    if (!win_el) {
        std::cerr << "ElementFromHandle failed for hwnd=0x" << std::hex << (uintptr_t)target << std::dec << "\n";
        return 3;
    }

    std::cout << "Probing for " << duration_sec << "s, polling every 200ms...\n";
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration_sec);
    int seq = 0;
    while (!interrupted.load() && std::chrono::steady_clock::now() < deadline) {
        std::vector<std::wstring> lines;
        std::wstring method;
        bool ok = uia.ReadCaptionText(win_el.Get(), lines, method);
        if (!ok) { std::cerr << "Caption control unavailable.\n"; return 2; }
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm;
        ::localtime_s(&tm, &now);
        wchar_t tbuf[32];
        ::wcsftime(tbuf, _countof(tbuf), L"%H:%M:%S", &tm);

        std::cout << "[" << WideToUtf8(tbuf) << " #" << seq << "] method=" << WideToUtf8(method)
                   << " ok=" << (ok ? "yes" : "no") << " lines=" << lines.size() << "\n";
        for (auto& l : lines) {
            std::cout << "    > " << WideToUtf8(l) << "\n";
        }
        ++seq;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return 0;
}

int CmdDumpTree(HWND hwnd, int depth, const std::string& out_path, bool include_text) {
    ccs::platform::UiaClient uia;
    if (!uia.Initialize()) return 1;

    auto win_el = uia.ElementFromHandle(hwnd);
    if (!win_el) return 3;

    auto nodes = uia.DumpTree(win_el.Get(), depth);

    json arr = json::array();
    for (auto& n : nodes) {
        json j;
        j["depth"] = n.depth;
        j["control_type"] = WideToUtf8(ccs::platform::ControlTypeName(n.control_type));
        j["automation_id"] = WideToUtf8(n.automation_id);
        j["class_name"] = WideToUtf8(n.class_name);
        j["is_offscreen"] = n.is_offscreen;
        j["text_pattern"] = WideToUtf8(n.support_text_pattern);
        if (include_text) j["name"] = WideToUtf8(n.name);
        else j["name_len"] = n.name.size();
        arr.push_back(j);
    }

    if (out_path.empty()) {
        std::cout << arr.dump(2) << "\n";
    } else {
        std::ofstream f(std::filesystem::path(std::u8string(out_path.begin(), out_path.end())), std::ios::binary);
        f << arr.dump(2);
        f.flush();
        if (!f) { std::cerr << "Failed to write dump file\n"; return 1; }
        std::cout << "Wrote " << nodes.size() << " nodes to " << out_path << "\n";
    }
    return 0;
}

int CmdRecord(HWND hwnd, int duration_sec, const std::string& outdir) {
    if (!hwnd) hwnd = FindCaptionWindow();
    if (!hwnd || !::IsWindow(hwnd)) {
        std::cerr << "No valid caption window found.\n";
        return 2;
    }
    // Prepare output dir.
    std::string dir = outdir;
    if (dir.empty()) {
        auto t = std::time(nullptr);
        std::tm tm_local;
        ::localtime_s(&tm_local, &t);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm_local);
        const auto path = (ccs::core::DefaultSessionsDirectory() / buf).u8string();
        dir.assign(path.begin(), path.end());
    }

    ccs::storage::JournalWriter writer;
    if (!writer.Open(dir)) {
        std::cerr << "Failed to open output dir: " << dir << "\n";
        return 1;
    }

    ccs::capture::CaptureConfig cfg;
    cfg.poll_interval_ms = 200;
    cfg.session_id = std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()) + "-" + std::to_string(GetCurrentProcessId());

    ccs::capture::CaptureWorker worker;
    if (!worker.Start(hwnd, cfg)) {
        std::cerr << "Failed to start CaptureWorker\n";
        return 1;
    }

    std::cout << "Recording for " << duration_sec << "s → " << dir << "\n";
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration_sec);
    uint64_t seq = 0;
    while (!interrupted.load() && std::chrono::steady_clock::now() < deadline) {
        std::vector<ccs::core::CaptionSnapshot> batch;
        if (worker.stats().state == ccs::core::CaptureState::TargetNotFound) break;
        if (worker.Drain(batch)) {
            for (size_t bi = 0; bi < batch.size(); ++bi) {
                if (!writer.Append(batch[bi], ++seq)) return 1;
            }
            if (!writer.Flush()) return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    worker.Stop();
    const bool failed = worker.stats().state == ccs::core::CaptureState::TargetNotFound;
    // Drain remaining.
    std::vector<ccs::core::CaptionSnapshot> rest;
    worker.Drain(rest);
    size_t nrest = rest.size();
    for (size_t ri = 0; ri < nrest; ++ri) {
        if (!writer.Append(rest[ri], ++seq)) return 1;
    }
    if (!writer.Flush()) return 1;
    writer.Close();

    if (seq > 0) {
        try {
            const auto base = std::filesystem::path(std::u8string(dir.begin(), dir.end()));
            const auto output = base / "exports" / std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(ccs::core::Clock::now().time_since_epoch()).count());
            ccs::exporter::ExportBundle(ccs::exporter::ReadJournal(base / "snapshots.jsonl"), output);
            std::cout << "TXT/SRT/VTT exported under the session exports directory.\n";
        } catch (const std::exception& e) {
            std::cerr << "Raw snapshots saved, but export failed: " << e.what() << "\n";
            return 1;
        }
    }
    if (failed) {
        std::cerr << "Capture target unavailable; partial snapshots were saved.\n";
        return 2;
    }
    if (seq == 0) {
        std::cerr << "No caption text captured. Check Live Caption and audio playback.\n";
        return 2;
    }
    std::cout << "Done. Wrote " << seq << " snapshots to " << dir << "\\snapshots.jsonl\n";
    return 0;
}

int CmdSelectPoint() {
    ccs::platform::UiaClient uia;
    if (!uia.Initialize()) return 1;

    std::cout << "Move mouse over the Chrome Live Caption bubble within 5 seconds...\n";
    for (int i = 5; i > 0; --i) {
        std::cout << "  " << i << "\r" << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    POINT p;
    ::GetCursorPos(&p);
    std::cout << "\nCursor at (" << p.x << "," << p.y << ")\n";

    auto el = uia.ElementFromPoint(p);
    if (!el) {
        std::cerr << "ElementFromPoint failed\n";
        return 2;
    }
    BSTR name = nullptr;
    BSTR cn = nullptr;
    CONTROLTYPEID ct = 0;
    el->get_CurrentName(&name);
    el->get_CurrentClassName(&cn);
    el->get_CurrentControlType(&ct);
    std::cout << "ControlType: " << WideToUtf8(ccs::platform::ControlTypeName(ct)) << "\n"
               << "ClassName : " << WideToUtf8(cn ? cn : L"") << "\n"
               << "Name      : " << WideToUtf8(name ? name : L"") << "\n";
    if (name) ::SysFreeString(name);
    if (cn) ::SysFreeString(cn);
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    spdlog::set_level(spdlog::level::warn);

    std::vector<std::wstring> args(argv + 1, argv + argc);
    auto has = [&](const wchar_t* flag) {
        for (auto& a : args) if (a == flag) return true;
        return false;
    };
    auto opt = [&](const wchar_t* flag) -> std::wstring {
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == flag) return args[i + 1];
        }
        return {};
    };

    int commands = 0;
    for (const auto& arg : args) {
        if (arg == L"--probe" || arg == L"--record" || arg == L"--dump-tree" ||
            arg == L"--select-point" || arg == L"--list-windows" || arg == L"--version" || arg == L"--export") ++commands;
    }
    try {
        std::set<std::wstring> seen;
        if (commands > 1) throw std::invalid_argument("Choose one command");
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& a = args[i];
            if (!seen.insert(a).second) throw std::invalid_argument("Duplicate option");
            if ((a == L"--duration" && !has(L"--probe") && !has(L"--record")) ||
                ((a == L"--depth" || a == L"--output" || a == L"--include-text") && !has(L"--dump-tree")) ||
                (a == L"--outdir" && !has(L"--record") && !has(L"--export")) ||
                (a == L"--hwnd" && !has(L"--probe") && !has(L"--record") && !has(L"--dump-tree")))
                throw std::invalid_argument("Option does not apply to command");
            if (a == L"--duration" || a == L"--depth" || a == L"--hwnd" ||
                a == L"--output" || a == L"--outdir" || a == L"--export") {
                if (++i == args.size() || args[i].starts_with(L"--") || args[i].empty())
                    throw std::invalid_argument("Missing option value");
                if (a == L"--duration" || a == L"--depth" || a == L"--hwnd") {
                    const auto& value = args[i];
                    if (value.front() == L'-' || value.front() == L'+' || iswspace(value.front()))
                        throw std::invalid_argument("Invalid numeric value");
                    size_t used = 0;
                    auto n = std::stoull(value, &used, a == L"--hwnd" ? 16 : 10);
                    if (used != value.size() || n == 0 ||
                        (a == L"--duration" && n > 86400) ||
                        (a == L"--depth" && n > 64))
                        throw std::invalid_argument("Numeric value out of range");
                }
            } else if (a != L"--help" && a != L"-h" && a != L"--include-text" &&
                       a != L"--probe" && a != L"--record" && a != L"--dump-tree" &&
                       a != L"--select-point" && a != L"--list-windows" && a != L"--version") {
                throw std::invalid_argument("Unknown option");
            }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << ". Try --help.\n";
        return 1;
    }
    if (args.empty() || has(L"--help") || has(L"-h")) {
        PrintHelp();
        return 0;
    }
    if (has(L"--export")) {
        try {
            const std::filesystem::path input(opt(L"--export"));
            auto output = std::filesystem::path(opt(L"--outdir"));
            if (output.empty()) output = input.parent_path() / "exports" /
                std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(ccs::core::Clock::now().time_since_epoch()).count());
            const auto segments = ccs::exporter::ReadJournal(input);
            ccs::exporter::ExportBundle(segments, output);
            std::cout << "Exported " << segments.size() << " segments (approximate capture times).\n";
            return 0;
        } catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
    }
    if (has(L"--version")) {
        std::cout << "caption_probe 0.2.0\n";
        return 0;
    }
    if (has(L"--list-windows")) {
        return CmdListWindows();
    }
    if (has(L"--probe")) {
        int dur = 10;
        auto sd = opt(L"--duration");
        if (!sd.empty()) dur = _wtoi(sd.c_str());
        HWND hwnd = nullptr;
        auto sh = opt(L"--hwnd");
        if (!sh.empty()) hwnd = reinterpret_cast<HWND>(_wcstoui64(sh.c_str(), nullptr, 16));
        return CmdProbe(dur, hwnd);
    }
    if (has(L"--dump-tree")) {
        HWND hwnd = nullptr;
        auto sh = opt(L"--hwnd");
        if (sh.empty()) { std::cerr << "--dump-tree requires --hwnd\n"; return 1; }
        hwnd = reinterpret_cast<HWND>(_wcstoui64(sh.c_str(), nullptr, 16));
        int depth = 8;
        auto sd = opt(L"--depth");
        if (!sd.empty()) depth = _wtoi(sd.c_str());
        auto so = opt(L"--output");
        return CmdDumpTree(hwnd, depth, WideToUtf8(so), has(L"--include-text"));
    }
    if (has(L"--select-point")) {
        return CmdSelectPoint();
    }
    if (has(L"--record")) {
        HWND hwnd = nullptr;
        auto sh = opt(L"--hwnd");
        if (!sh.empty()) hwnd = reinterpret_cast<HWND>(_wcstoui64(sh.c_str(), nullptr, 16));
        int dur = 10;
        auto sd = opt(L"--duration");
        if (!sd.empty()) dur = _wtoi(sd.c_str());
        auto so = opt(L"--outdir");
        return CmdRecord(hwnd, dur, WideToUtf8(so));
    }

    std::cerr << "Unknown command. Try --help.\n";
    return 1;
}
