#pragma once
// Locate the Chrome Live Caption floating window.
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace ccs::platform {

struct WindowInfo {
    HWND        hwnd = nullptr;
    uint32_t    pid = 0;
    std::wstring exe_path;
    std::wstring class_name;
    std::wstring title;
    RECT        rect = {};
    bool        visible = false;
    int         score = 0;
    std::wstring score_reason;
};

std::vector<WindowInfo> FindChromeCaptionWindows();
bool GetWindowInfo(HWND hwnd, WindowInfo& out);
const WindowInfo* PickBest(const std::vector<WindowInfo>& list);

} // namespace ccs::platform
