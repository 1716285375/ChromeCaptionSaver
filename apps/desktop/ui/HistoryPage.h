#pragma once

inline void Desktop::HistoryPage(HWND owner, float s) {
  ImGui::PushFont(nullptr, 21);
  ImGui::TextUnformatted("历史记录");
  ImGui::PopFont();
  ImGui::BeginDisabled(history_scan.valid());
  if (ImGui::Button("刷新会话列表"))
    ScanHistory();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(offline.valid());
  if (ImGui::Button("从已有快照导出…")) {
    auto input = PickPath(owner, false);
    if (!input.empty())
      ExportFile(input);
  }
  ImGui::EndDisabled();
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##history-filter", "按会话日期 / 名称筛选",
                           &history_filter);
  const float available = ImGui::GetContentRegionAvail().x;
  history_width = ccs::ui::HistoryLayout::Clamp(history_width, available / s);
  ImGui::BeginChild("sessions", {history_width * s, 0});
  if (histories.empty()) {
    ImGui::TextWrapped(history_scan.valid()
                           ? "正在读取会话列表…"
                           : "还没有已保存会话。点击刷新，或从已有快照导出。");
  }
  std::vector<size_t> matches;
  for (size_t i = 0; i < histories.size(); ++i) {
    if (history_filter.empty() ||
        PathText(histories[i].filename()).find(history_filter) !=
            std::string::npos)
      matches.push_back(i);
  }
  ImGuiListClipper clip;
  clip.Begin(static_cast<int>(matches.size()));
  while (clip.Step()) {
    for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
      const auto &file = histories[matches[row]];
      const auto label = PathText(file.filename());
      if (ImGui::Selectable(label.c_str(), history_file == file)) {
        history_file = file;
        history_reader = SelectableTextView{};
        ImGui::ClearActiveID();
        history_preview.Select(file, [](const fs::path &folder) {
          return TranscriptText(
              ccs::exporter::ReadJournal(folder / "snapshots.jsonl"));
        });
      }
    }
  }
  ImGui::EndChild();
  ImGui::SameLine(0, 0);
  ccs::ui::HistorySplitter(history_width, available, s);
  ImGui::SameLine(0, 0);
  ImGui::BeginChild("history-preview");
  ImGui::BeginDisabled(history_file.empty() || offline.valid());
  if (ImGui::Button("导出所选会话"))
    ExportFile(history_file / "snapshots.jsonl");
  ImGui::SameLine();
  if (ImGui::Button("打开会话目录"))
    ShellExecuteW(owner, L"open", history_file.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
  ImGui::EndDisabled();
  if (!history_file.empty())
    ImGui::TextDisabled("%s", PathText(history_file.filename()).c_str());
  if (!history_preview.error.empty())
    ImGui::TextColored(ccs::ui::Error, "%s", history_preview.error.c_str());
  const char *title = history_file.empty()      ? "选择左侧会话查看原文"
                      : history_preview.loading ? "正在读取所选会话…"
                                                : "会话原文预览";
  ReadPanel(title, "history", history_preview.text, history_reader,
            std::max(160 * s, ImGui::GetContentRegionAvail().y - 35 * s), 3,
            false);
  ImGui::EndChild();
}
