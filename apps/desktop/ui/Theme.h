#pragma once
#include "imgui.h"
#include <algorithm>
namespace ccs::ui {
inline ImVec4 Color(unsigned n, float alpha = 1) {
  return {((n >> 16) & 255) / 255.f, ((n >> 8) & 255) / 255.f,
          (n & 255) / 255.f, alpha};
}
inline const ImVec4 Jade = Color(0x43D3AC), Gold = Color(0xD8B47C),
                    Muted = Color(0x6F808A), Secondary = Color(0xA2AFB6),
                    Warning = Color(0xE6B36C), Error = Color(0xF08389);
inline void ApplyTheme() {
  ImGui::StyleColorsDark();
  auto &s = ImGui::GetStyle();
  s.WindowPadding = {24, 18};
  s.FramePadding = {12, 8};
  s.ItemSpacing = {12, 10};
  s.ItemInnerSpacing = {8, 6};
  s.WindowRounding = 14;
  s.ChildRounding = 14;
  s.FrameRounding = 8;
  s.PopupRounding = 10;
  s.GrabRounding = 8;
  s.ScrollbarRounding = 9;
  s.WindowBorderSize = 0;
  s.ChildBorderSize = 1;
  s.FrameBorderSize = 1;
  s.ScrollbarSize = 10;
  s.GrabMinSize = 12;
  auto *c = s.Colors;
  c[ImGuiCol_WindowBg] = Color(0x0D1116);
  c[ImGuiCol_ChildBg] = Color(0x171E26);
  c[ImGuiCol_PopupBg] = Color(0x1D2731);
  c[ImGuiCol_Text] = Color(0xECF2F1);
  c[ImGuiCol_TextDisabled] = Muted;
  c[ImGuiCol_Border] = c[ImGuiCol_Separator] = Color(0x2B3742);
  c[ImGuiCol_FrameBg] = Color(0x141C24);
  c[ImGuiCol_FrameBgHovered] = Color(0x23303B);
  c[ImGuiCol_FrameBgActive] = Color(0x233D3B);
  c[ImGuiCol_Button] = Color(0x1D2731);
  c[ImGuiCol_ButtonHovered] = Color(0x283F43);
  c[ImGuiCol_ButtonActive] = Color(0x205448);
  c[ImGuiCol_Header] = Color(0x184539);
  c[ImGuiCol_HeaderHovered] = Color(0x235447);
  c[ImGuiCol_HeaderActive] = Color(0x276651);
  c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = c[ImGuiCol_NavCursor] = Jade;
  c[ImGuiCol_SliderGrabActive] = Color(0x67E4BE);
  c[ImGuiCol_TextSelectedBg] = Color(0x43D3AC, .26f);
  c[ImGuiCol_ScrollbarBg] = Color(0x111820);
  c[ImGuiCol_ScrollbarGrab] = Color(0x2B3742);
  c[ImGuiCol_ScrollbarGrabHovered] = Color(0x43616A);
}
struct HistoryLayout {
  static constexpr float DefaultListWidth = 240;
  static constexpr float MinListWidth = 160;
  static constexpr float MinPreviewWidth = 300;
  static constexpr float DividerWidth = 12;
  static float Clamp(float width, float available) {
    return std::clamp(
        width, MinListWidth,
        std::max(MinListWidth, available - MinPreviewWidth - DividerWidth));
  }
};
struct Layout {
  float sidebar, current;
  bool compact;
  static Layout For(float width, float height, float scale) {
    return {width / scale >= 1440 ? 220.f : 70.f,
            height / scale < 760 ? 150.f : 200.f, width / scale < 1440};
  }
};
} // namespace ccs::ui
