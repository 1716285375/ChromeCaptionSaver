#include "ChromeCaptionSaver/platform/WindowLocator.h"
#include <psapi.h>
#include <spdlog/spdlog.h>
#include <algorithm>

#pragma comment(lib, "psapi.lib")

namespace ccs::platform {

namespace {

std::wstring GetWindowClassName(HWND hwnd) {
    wchar_t buf[512] = {};
    int len = ::GetClassNameW(hwnd, buf, _countof(buf));
    return std::wstring(buf, len);
}

std::wstring GetWindowTitle(HWND hwnd) {
    int len = ::GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring title(len + 1, L'\0');
    int actual = ::GetWindowTextW(hwnd, title.data(), len + 1);
    title.resize(actual);
    return title;
}

std::wstring GetProcessExePath(DWORD pid) {
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t buf[MAX_PATH] = {};
    DWORD sz = _countof(buf);
    std::wstring path;
    if (::QueryFullProcessImageNameW(h, 0, buf, &sz)) {
        path.assign(buf, sz);
    }
    ::CloseHandle(h);
    return path;
}

// Heuristic: does this exe path look like a real Chrome browser (not crashpad, not helper)?
bool IsRealChromeExe(const std::wstring& path) {
    std::wstring lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    if (lower.find(L"\\chrome.exe") == std::wstring::npos) return false;
    // Exclude helper / crashpad / setup paths that also contain chrome.exe
    if (lower.find(L"\\chrome application") != std::wstring::npos) return true;
    if (lower.find(L"\\google\\chrome\\application\\chrome.exe") != std::wstring::npos) return true;
    if (lower.find(L"\\chrome\\chrome.exe") != std::wstring::npos) return true;
    return false;
}

// Score a candidate window. We do NOT hard-code title strings because Chrome
// localizes "Live Caption" / "实时字幕". Instead we look for:
//  - small, floating, visible, non-minimized top-level window owned by chrome.exe
//  - width/height typical of a caption bubble (e.g. 200..1200 x 30..200)
int ScoreCandidate(WindowInfo& w) {
    int score = 0;
    std::wstring reason;

    if (!IsRealChromeExe(w.exe_path)) return 0;
    score += 10;
    reason += L"exe=chrome;";

    if (w.visible) {
        score += 5;
        reason += L"visible;";
    } else {
        return score; // invisible windows are not candidates
    }

    int ww = w.rect.right - w.rect.left;
    int wh = w.rect.bottom - w.rect.top;
    if (ww < 10 || wh < 10) return 0;

    // Caption bubbles are typically wide and short
    if (wh >= 20 && wh <= 250) { score += 5; reason += L"short-height;"; }
    if (ww >= 150 && ww <= 1600) { score += 3; reason += L"wide;"; }
    if (ww > wh * 2) { score += 3; reason += L"landscape;"; }

    // Chrome caption bubble class names observed across versions vary;
    // don't hard-code. But small top-level chrome windows with no taskbar
    // presence are interesting.
    LONG_PTR ex = ::GetWindowLongPtrW(w.hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) { score += 4; reason += L"toolwindow;"; }
    if (ex & WS_EX_NOACTIVATE) { score += 2; reason += L"noactivate;"; }

    w.score_reason = reason;
    return score;
}

struct EnumCtx {
    std::vector<WindowInfo>* out;
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lparam) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lparam);
    if (!::IsWindowVisible(hwnd)) return TRUE;

    WindowInfo info;
    info.hwnd = hwnd;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    info.pid = pid;
    info.exe_path = GetProcessExePath(pid);

    // Quick filter: only chrome processes
    if (!IsRealChromeExe(info.exe_path)) return TRUE;

    info.class_name = GetWindowClassName(hwnd);
    info.title = GetWindowTitle(hwnd);
    ::GetWindowRect(hwnd, &info.rect);
    info.visible = ::IsWindowVisible(hwnd) != FALSE;

    int s = ScoreCandidate(info);
    if (s > 0) {
        info.score = s;
        ctx->out->push_back(std::move(info));
    }
    return TRUE;
}

} // namespace

std::vector<WindowInfo> FindChromeCaptionWindows() {
    std::vector<WindowInfo> result;
    EnumCtx ctx{ &result };
    ::EnumWindows(&EnumProc, reinterpret_cast<LPARAM>(&ctx));
    std::sort(result.begin(), result.end(),
              [](const WindowInfo& a, const WindowInfo& b) { return a.score > b.score; });
    spdlog::debug("FindChromeCaptionWindows: {} candidates", result.size());
    return result;
}

bool GetWindowInfo(HWND hwnd, WindowInfo& out) {
    if (!::IsWindow(hwnd)) return false;
    out = {};
    out.hwnd = hwnd;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    out.pid = pid;
    out.exe_path = GetProcessExePath(pid);
    out.class_name = GetWindowClassName(hwnd);
    out.title = GetWindowTitle(hwnd);
    ::GetWindowRect(hwnd, &out.rect);
    out.visible = ::IsWindowVisible(hwnd) != FALSE;
    out.score = ScoreCandidate(out);
    return true;
}

const WindowInfo* PickBest(const std::vector<WindowInfo>& list) {
    if (list.empty()) return nullptr;
    return &list.front(); // already sorted by score desc
}

} // namespace ccs::platform
