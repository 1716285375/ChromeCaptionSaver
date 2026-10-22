#pragma once
#include "imgui.h"
#include <filesystem>
namespace ccs::ui {
inline void LoadTypography() {
  wchar_t root[MAX_PATH];
  GetWindowsDirectoryW(root, MAX_PATH);
  const auto folder = std::filesystem::path(root) / L"Fonts";
  auto &io = ImGui::GetIO();
  ImFont *font = nullptr;
  for (const auto *name : {L"NotoSansCJK-Regular.ttc", L"msyh.ttc",
                           L"YuGothR.ttc", L"meiryo.ttc"}) {
    auto path = folder / name;
    if (!std::filesystem::exists(path))
      continue;
    auto utf = path.u8string();
    std::string file(utf.begin(), utf.end());
    font = io.Fonts->AddFontFromFileTTF(file.c_str(), 15);
    if (font)
      break;
  }
  if (!font)
    font = io.Fonts->AddFontDefault();
  for (const auto *name : {L"YuGothR.ttc", L"meiryo.ttc"}) {
    auto path = folder / name;
    if (!std::filesystem::exists(path))
      continue;
    auto utf = path.u8string();
    std::string file(utf.begin(), utf.end());
    ImFontConfig config;
    config.MergeMode = true;
    io.Fonts->AddFontFromFileTTF(file.c_str(), 15, &config);
    break;
  }
  // ImGui 1.92 rasterizes merged CJK glyphs at the requested size and DPI on
  // demand.
  io.FontDefault = font;
}
} // namespace ccs::ui
