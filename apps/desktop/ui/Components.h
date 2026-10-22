#pragma once
#include "Motion.h"
#include "Theme.h"
namespace ccs::ui {
inline bool reduce_motion = false;
inline void Badge(const char *text, ImVec4 color) {
  auto p = ImGui::GetCursorScreenPos();
  auto size = ImGui::CalcTextSize(text);
  float pad = ImGui::GetStyle().FramePadding.y;
  ImGui::GetWindowDrawList()->AddRectFilled(
      p, {p.x + size.x + 20, p.y + size.y + pad},
      ImGui::GetColorU32({color.x, color.y, color.z, .09f}), 7);
  ImGui::SetCursorScreenPos({p.x + 10, p.y + pad / 2});
  ImGui::TextColored(color, "%s", text);
}
inline bool Action(const char *label, bool primary, float scale = 1) {
  const auto pos = ImGui::GetCursorScreenPos();
  auto size = ImGui::CalcTextSize(label);
  const bool hovered = ImGui::IsMouseHoveringRect(
      pos, {pos.x + size.x + ImGui::GetStyle().FramePadding.x * 2,
            pos.y + 38 * scale});
  auto *blend =
      ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID(label) ^ 0x73A92u, 0);
  *blend = reduce_motion ? (hovered ? 1.f : 0.f)
                         : Approach(*blend, hovered ? 1.f : 0.f,
                                    ImGui::GetIO().DeltaTime, 22);
  auto normal = Color(primary ? 0x12392F : 0x1D2731),
       over = Color(primary ? 0x205448 : 0x283F43);
  ImVec4 fill{normal.x + (over.x - normal.x) * (*blend),
              normal.y + (over.y - normal.y) * (*blend),
              normal.z + (over.z - normal.z) * (*blend), 1};
  ImGui::PushStyleColor(ImGuiCol_Button, fill);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fill);
  if (primary)
    ImGui::PushStyleColor(ImGuiCol_Text, Jade);
  bool hit = ImGui::Button(label, {0, 38 * scale});
  if (primary)
    ImGui::PopStyleColor();
  ImGui::PopStyleColor(2);
  return hit;
}
inline bool Nav(const char *label, const char *short_label, bool selected,
                bool compact, float scale, bool reduced) {
  ImGui::PushID(label);
  auto p = ImGui::GetCursorScreenPos();
  auto id = ImGui::GetID("fade");
  auto *store = ImGui::GetStateStorage();
  auto *fade = store->GetFloatRef(id, selected ? 1.f : 0.f);
  *fade = reduced
              ? (selected ? 1.f : 0.f)
              : Approach(*fade, selected ? 1.f : 0.f, ImGui::GetIO().DeltaTime);
  ImGui::PushStyleColor(ImGuiCol_Button, Color(0x43D3AC, .1f * (*fade)));
  ImGui::PushStyleColor(ImGuiCol_Text, selected ? Jade : Secondary);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,
                      compact ? ImVec2(.5f, .5f) : ImVec2(0, .5f));
  bool hit = ImGui::Button(compact ? short_label : label, {-1, 44 * scale});
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
  if (*fade > .01f)
    ImGui::GetWindowDrawList()->AddRectFilled(
        {p.x, p.y + 10 * scale}, {p.x + 3 * scale, p.y + 34 * scale},
        ImGui::GetColorU32(Color(0x43D3AC, *fade)), 2);
  if (compact && ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", label);
  ImGui::PopID();
  return hit;
}
inline void HistorySplitter(float &width, float available, float scale) {
  const auto position = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(
      "history-divider",
      {HistoryLayout::DividerWidth * scale, ImGui::GetContentRegionAvail().y});
  const bool active = ImGui::IsItemActive();
  const bool hovered = ImGui::IsItemHovered();
  if (active || hovered)
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  if (active)
    width = HistoryLayout::Clamp(width + ImGui::GetIO().MouseDelta.x / scale,
                                 available / scale);
  const float x = position.x + HistoryLayout::DividerWidth * scale / 2;
  ImGui::GetWindowDrawList()->AddLine(
      {x, position.y}, {x, position.y + ImGui::GetItemRectSize().y},
      ImGui::GetColorU32(active || hovered ? Jade : Color(0x2B3742)), scale);
}
} // namespace ccs::ui
