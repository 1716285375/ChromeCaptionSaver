#pragma once

inline void Desktop::SettingsPage(HWND owner,
                                  const ccs::capture::SessionView &view,
                                  float s) {
  using namespace ccs::ui;
  ImGui::PushFont(nullptr, 21);
  ImGui::TextUnformatted("设置");
  ImGui::PopFont();
  ImGui::TextColored(Secondary, "字幕来源");
  ImGui::BeginDisabled(view.running || discovery.valid());
  if (Action(discovery.valid() ? "正在检测…" : "检测字幕窗口", true, s))
    Refresh();
  ImGui::EndDisabled();
  ImGui::BeginDisabled(view.running);
  ImGui::SetNextItemWidth(-1);
  auto label = selected >= 0 && selected < static_cast<int>(targets.size())
                   ? Utf8(targets[static_cast<size_t>(selected)].title)
                   : std::string("自动寻找 Chrome 实时字幕窗口");
  if (ImGui::BeginCombo("##target", label.c_str())) {
    if (ImGui::Selectable("自动寻找字幕窗口", selected == -1))
      selected = -1;
    for (size_t i = 0; i < targets.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Selectable(Utf8(targets[i].title).c_str(),
                            selected == static_cast<int>(i)))
        selected = static_cast<int>(i);
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  if (ImGui::CollapsingHeader("高级来源设置")) {
    ImGui::Checkbox("手动输入窗口句柄", &manual);
    if (manual) {
      ImGui::SetNextItemWidth(-1);
      ImGui::InputTextWithHint("##hwnd", "0x123456", handle.data(),
                               handle.size());
    }
  }
  ImGui::Spacing();
  ImGui::TextColored(Secondary, "保存位置");
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##directory", directory.data(), directory.size(),
                       ImGuiInputTextFlags_EnterReturnsTrue))
    settings_dirty = true;
  if (Action("选择保存目录", false, s)) {
    auto path = PickPath(owner, true);
    if (!path.empty()) {
      std::snprintf(directory.data(), directory.size(), "%s",
                    PathText(path).c_str());
      settings_dirty = true;
    }
  }
  ImGui::EndDisabled();
  ImGui::Spacing();
  ImGui::TextColored(Secondary, "阅读与外观");
  ImGui::SetNextItemWidth(280 * s);
  if (ImGui::SliderFloat("阅读字号", &reading_size, 16, 25, "%.0f px"))
    settings_dirty = true;
  if (ImGui::Checkbox("默认自动跟随最新字幕", &follow))
    settings_dirty = true;
  if (ImGui::Checkbox("减少动画", &motion.reduced))
    settings_dirty = true;
  ImGui::TextDisabled(
      "Graphite / Jade / Brass · 跟随系统 DPI · 中文与日文字体自动回退");
  ImGui::Spacing();
  ImGui::TextColored(Secondary, "快捷键");
  ImGui::TextUnformatted("Ctrl+A  当前阅读区全选\nCtrl+C  复制选中内容\nCtrl+F "
                         " 搜索文稿\nEsc       退出选择，恢复跟随");
  ImGui::TextWrapped("拖动选择、滚轮阅读或搜索会暂停跟随，避免新字幕打断阅读。"
                     "设置会自动保存在项目 data 目录。");
}
