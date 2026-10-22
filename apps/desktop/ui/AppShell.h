#pragma once

inline void Desktop::Draw(HWND owner, float scale, bool render_test,
                          bool static_preview) {
  Poll();
  ccs::capture::SessionView preview;
  auto &view = render_test ? preview : cached;
  auto bridge = relay.View();
  ccs::ui::reduce_motion = motion.reduced;
  if (render_test) {
    ++test_frames;
    if (!static_preview)
      page = (test_frames / 2) % 4;
    view.stable_latest = "把声音留成文字，让阅读保持专注。";
    view.stable_previous = "前一句已经保存，可以随时选择与复制。";
    view.candidate_text = "日本語の字幕を読み取っています。";
    view.stable_text = "00:00:12  把声音留成文字，让阅读保持专注。\n00:00:18  "
                       "日本語の字幕も、読みやすく。";
    view.transcript_text = view.stable_text;
    view.snapshots = 12;
    view.stable_count = 2;
    notice = "界面预览 · 示例内容，未启动采集或投递";
  }
  if (!view.running)
    stopping = false;
  if (bridge.queue.completed != previous_completed) {
    last_completed_at = ImGui::GetTime();
    previous_completed = bridge.queue.completed;
  }
  if (notice != last_notice) {
    notice_at = ImGui::GetTime();
    last_notice = notice;
  }
  motion.Tick(page != previous_page, view.stable_latest != last_caption,
              !notice.empty() && ImGui::GetTime() - notice_at < 7);
  previous_page = page;
  last_caption = view.stable_latest;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    live_reader.Resume();
    transcript_reader.Resume();
    reply_reader.Resume();
    history_reader.Resume();
    follow = true;
  }
  if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) &&
      ImGui::GetActiveID() == 0)
    (last_reader == 0   ? live_reader
     : last_reader == 2 ? reply_reader
     : last_reader == 3 ? history_reader
                        : transcript_reader)
        .select_all = true;
  const auto viewport = ImGui::GetMainViewport();
  auto layout =
      ccs::ui::Layout::For(viewport->WorkSize.x, viewport->WorkSize.y, scale);
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("ChromeCaptionSaver", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoScrollbar);
  ImGui::PushFont(nullptr, 15);
  ImGui::PushFont(nullptr, 25);
  ImGui::TextUnformatted("字幕工作室");
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::TextColored(ccs::ui::Gold, "  /  CAPTION SAVER");
  bool disconnected = view.state == ccs::core::CaptureState::TargetNotFound;
  ImGui::SameLine(std::max(420 * scale, ImGui::GetWindowWidth() - 520 * scale));
  ccs::ui::Badge(disconnected                       ? "Chrome 已断开"
                 : view.running || !targets.empty() ? "Chrome 已连接"
                                                    : "Chrome 待检测",
                 disconnected                       ? ccs::ui::Error
                 : view.running || !targets.empty() ? ccs::ui::Jade
                                                    : ccs::ui::Muted);
  ImGui::SameLine();
  ImGui::TextDisabled("%s  ·  %02lld:%02lld:%02lld", StateName(view.state),
                      static_cast<long long>(view.elapsed_ms / 3600000),
                      static_cast<long long>(view.elapsed_ms / 60000 % 60),
                      static_cast<long long>(view.elapsed_ms / 1000 % 60));
  ImGui::Spacing();
  ImGui::Spacing();
  const float footer = 38 * scale;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ccs::ui::Color(0x111820));
  ImGui::BeginChild("navigation", {layout.sidebar * scale, -footer});
  const char *labels[] = {"录制与阅读", "ChatGPT 联动", "历史记录", "设置"};
  const char *short_labels[] = {"录", "AI", "史", "设"};
  for (int i = 0; i < 4; ++i)
    if (ccs::ui::Nav(labels[i], short_labels[i], page == i, layout.compact,
                     scale, motion.reduced)) {
      page = i;
      if (i == 2 && histories.empty() && !render_test)
        ScanHistory();
    }
  if (!layout.compact) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("CHROME LIVE CAPTION");
    ImGui::TextColored(view.running ? ccs::ui::Jade : ccs::ui::Secondary, "%s",
                       StateName(view.state));
    ImGui::TextDisabled("%llu 条稳定字幕",
                        static_cast<unsigned long long>(view.stable_count));
    ImGui::Spacing();
    ImGui::TextColored(bridge.connected ? ccs::ui::Jade : ccs::ui::Muted, "%s",
                       bridge.connected ? "AI 网页已连接" : "AI 尚未配对");
    ImGui::TextDisabled("已完成 %llu / 待发 %zu",
                        static_cast<unsigned long long>(bridge.queue.completed),
                        bridge.queue.pending);
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, {0, 0, 0, 0});
  ImGui::BeginChild("workspace", {0, -footer});
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, .65f + .35f * motion.page);
  switch (page) {
  case 0:
    CapturePage(owner, view, scale, layout);
    break;
  case 1:
    BridgePage(owner, view, bridge, scale);
    break;
  case 2:
    HistoryPage(owner, scale);
    break;
  default:
    SettingsPage(owner, view, scale);
    break;
  }
  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::PopStyleColor();
  if (motion.toast > .02f) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ccs::ui::Color(0xD8B47C, motion.toast));
    ImGui::TextUnformatted(notice.c_str());
    ImGui::PopStyleColor();
  } else {
    auto saved = view.directory.empty()
                     ? std::string("原始字幕自动保存至所选目录")
                     : "已保存至 " + PathText(view.directory.filename());
    ImGui::TextDisabled("%s  ·  %llu 条稳定字幕  ·  %s", StateName(view.state),
                        static_cast<unsigned long long>(view.stable_count),
                        saved.c_str());
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", view.directory.empty()
                                  ? directory.data()
                                  : PathText(view.directory).c_str());
  }
  ImGui::PopFont();
  ImGui::End();
}
