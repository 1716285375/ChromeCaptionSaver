#pragma once

inline void Desktop::CapturePage(HWND owner,
                                 const ccs::capture::SessionView &view, float s,
                                 const ccs::ui::Layout &layout) {
  ImGui::PushFont(nullptr, 21);
  ImGui::TextUnformatted("录制与阅读");
  ImGui::PopFont();
  ImGui::SameLine(std::max(200 * s, ImGui::GetWindowWidth() - 330 * s));
  CaptureActions(view, s);
  if (view.state == ccs::core::CaptureState::TargetNotFound) {
    ImGui::TextColored(ccs::ui::Error, "字幕来源已断开，原始录制已保留。");
    if (ImGui::Button("重新检测字幕窗口"))
      Refresh();
  }
  ImGui::Spacing();
  ImGui::BeginChild("current-caption", {0, layout.current * s});
  ImGui::TextColored(ccs::ui::Secondary, "当前字幕");
  ImGui::SameLine();
  if (ImGui::Checkbox("自动跟随", &follow)) {
    settings_dirty = true;
    if (follow) {
      live_reader.Resume();
      transcript_reader.Resume();
    }
  }
  if (!view.stable_previous.empty())
    ImGui::TextColored(ccs::ui::Muted, "%s", view.stable_previous.c_str());
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, .7f + .3f * motion.caption);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, {0, 0, 0, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
  ImGui::PushFont(nullptr, 25);
  if (live_reader.Draw("##live",
                       view.stable_latest.empty()
                           ? (view.candidate_text.empty() ? "等待字幕…"
                                                          : view.candidate_text)
                           : view.stable_latest,
                       {-1, 60 * s}, follow))
    last_reader = 0;
  ImGui::PopFont();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  ImGui::TextColored(ccs::ui::Secondary, "%s",
                     view.candidate_text.empty()
                         ? "候选字幕将在这里显示，稳定前不会混入文稿。"
                         : "候选 · 正在变化");
  if (!view.candidate_text.empty() && !view.stable_latest.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ccs::ui::Secondary);
    ImGui::TextWrapped("%s", view.candidate_text.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::EndChild();
  ImGui::Spacing();
  if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
    search_visible = true;
    focus_search = true;
  }
  if (ImGui::Button("搜索文稿")) {
    search_visible = !search_visible;
    focus_search = true;
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("仅显示稳定字幕", &stable_only)) {
    settings_dirty = true;
    transcript_reader.Resume();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("A-")) {
    reading_size = std::max(16.f, reading_size - 1);
    settings_dirty = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("A+")) {
    reading_size = std::min(25.f, reading_size + 1);
    settings_dirty = true;
  }
  if (search_visible) {
    ImGui::SetNextItemWidth(
        std::max(180 * s, ImGui::GetContentRegionAvail().x - 180 * s));
    if (focus_search) {
      ImGui::SetKeyboardFocusHere();
      focus_search = false;
    }
    bool go =
        ImGui::InputTextWithHint("##search", "搜索中文 / 日本語 / English",
                                 &search, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("查找下一处") || go) {
      transcript_reader.Find(search);
      follow = false;
    }
    if (!transcript_reader.search_status.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", transcript_reader.search_status.c_str());
    }
  }
  const auto &text = stable_only ? view.stable_text : view.transcript_text;
  ReadPanel("完整文稿", "transcript", text, transcript_reader,
            std::max(160 * s, ImGui::GetContentRegionAvail().y - 110 * s), 1,
            follow);
  Exports(owner, view, s);
  ImGui::TextDisabled(
      "时间戳为采集近似值。候选内容可能修订；原始录制始终保留。");
}
