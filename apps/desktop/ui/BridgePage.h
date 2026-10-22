#pragma once

inline void Desktop::BridgePage(HWND owner,
                                const ccs::capture::SessionView &view,
                                const ccs::chat::BridgeView &bridge, float s) {
  using namespace ccs::ui;
  ImGui::PushFont(nullptr, 21);
  ImGui::TextUnformatted("ChatGPT 联动");
  ImGui::PopFont();
  ImGui::TextColored(bridge.connected ? Jade : Warning, "%s",
                     bridge.connected ? "网页已连接 · 使用专用 ChatGPT 会话"
                                      : "等待配对 · 录制与导出仍可独立使用");
  if (Action("复制连接码", true, s)) {
    ImGui::SetClipboardText(relay.PairCode().c_str());
    notice = "连接码已复制，在扩展中粘贴并绑定当前标签页。";
  }
  ImGui::SameLine();
  if (Action("打开扩展目录", false, s)) {
    std::array<wchar_t, 32768> exe{};
    GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    auto folder = fs::path(exe.data()).parent_path() / "ChatGPTBridge";
    ShellExecuteW(owner, L"open", folder.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
  }
  ImGui::SameLine();
  if (Action("重新检测", false, s))
    notice = bridge.connected
                 ? "网页心跳正常。"
                 : "请刷新 ChatGPT，并在扩展中使用连接码重新配对。";
  if (!bridge.connected) {
    ImGui::TextWrapped(
        "1  Chrome 扩展页加载 ChatGPTBridge 文件夹\n2  刷新已登录的 ChatGPT "
        "页面\n3  在扩展中粘贴连接码并绑定当前标签页");
  }
  if (ImGui::CollapsingHeader("每句使用的提示词",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BeginDisabled(bridge.queue.enabled);
    ImGui::InputTextMultiline("##instruction", &instruction, {-1, 70 * s});
    ImGui::EndDisabled();
  }
  ImGui::BeginDisabled(bridge.queue.enabled || !bridge.connected ||
                       instruction.empty());
  if (Action("启动联动", true, s)) {
    try {
      relay.Start(instruction,
                  view.directory.empty()
                      ? ccs::core::DefaultSessionsDirectory() / "chatgpt"
                      : view.directory);
      intake_paused = false;
      bridge_started = ImGui::GetTime();
      notice = "已启动：新字幕稳定后入队，上一条回答确认后再发送下一条。";
    } catch (const std::exception &e) {
      notice = e.what();
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!bridge.queue.enabled);
  if (Action(intake_paused ? "继续入队" : "暂停新字幕入队", false, s)) {
    intake_paused = !intake_paused;
    notice = intake_paused
                 ? "已暂停新增投递，现有队列继续处理；新字幕仍保存到原始录制。"
                 : "恢复新增字幕入队。";
  }
  ImGui::SameLine();
  if (Action("停止联动", false, s))
    ImGui::OpenPopup("停止联动？");
  ImGui::EndDisabled();
  if (ImGui::BeginPopupModal("停止联动？", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("停止会清空待发送队列；原始录制保留。\n需要保留投递"
                           "内容时，请先导出未完成项。");
    if (ImGui::Button("停止并清空队列")) {
      relay.Stop();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("取消"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::BeginDisabled(!bridge.queue.enabled || !bridge.connected);
  if (Action("发送测试字幕", false, s))
    relay.Add("Hello. This is a subtitle translation connection test.");
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(offline.valid());
  if (Action("导出未完成项", false, s)) {
    auto text = relay.PendingText();
    auto root = Path(directory.data());
    offline = std::async(std::launch::async, [text, root] {
      auto folder = root / "unsent";
      fs::create_directories(folder);
      auto file =
          folder /
          ("unsent-" +
           std::to_string(ccs::core::Clock::now().time_since_epoch().count()) +
           ".txt");
      std::ofstream out(file, std::ios::binary);
      out << text;
      if (!out)
        throw std::runtime_error("未完成项导出失败。");
      return folder;
    });
  }
  ImGui::EndDisabled();
  const char *labels[] = {"已确认完成", "待发送", "网页提交待回复",
                          "失败待检查"};
  uint64_t values[] = {bridge.queue.completed, bridge.queue.pending,
                       bridge.queue.submitted, bridge.queue.failed};
  float w =
      (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) /
      4;
  for (int i = 0; i < 4; ++i) {
    ImGui::PushID(i);
    ImGui::BeginChild("metric", {w, 70 * s});
    ImGui::TextDisabled("%s", labels[i]);
    ImGui::PushFont(nullptr, 24);
    ImGui::TextColored(
        i == 3 && values[i] ? Error : ImGui::GetStyleColorVec4(ImGuiCol_Text),
        "%llu", static_cast<unsigned long long>(values[i]));
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopID();
    if (i < 3)
      ImGui::SameLine();
  }
  ImGui::TextColored(bridge.queue.failed           ? Error
                     : bridge.queue.pending >= 256 ? Warning
                                                   : Jade,
                     "%s", bridge.queue.phase.c_str());
  if (bridge.queue.in_flight && !bridge.queue.submitted)
    ImGui::TextColored(Warning,
                       "扩展已领取任务，等待网页确认提交；尚未计为完成。");
  if (!bridge.queue.detail.empty())
    ImGui::TextWrapped("%s", bridge.queue.detail.c_str());
  if (!bridge.error.empty())
    ImGui::TextColored(Error, "%s", bridge.error.c_str());
  if (bridge.queue.pending >= 256)
    ImGui::TextColored(Warning, "队列积压：可暂停新增入队，或先导出未完成项。");
  if (bridge_started > 0) {
    double minutes = (ImGui::GetTime() - bridge_started) / 60;
    ImGui::TextDisabled("本次平均 %.1f 条 / 分钟",
                        minutes > 0 ? bridge.queue.completed / minutes : 0);
  }
  if (last_completed_at > 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("最近完成 %.0f 秒前",
                        ImGui::GetTime() - last_completed_at);
  }
  if (bridge.since_activity_ms >= 0)
    ImGui::TextDisabled(
        "最近网页活动 %lld 秒前  /  最老待发 %lld:%02lld",
        static_cast<long long>(bridge.since_activity_ms / 1000),
        static_cast<long long>(bridge.queue.oldest_wait_ms / 60000),
        static_cast<long long>(bridge.queue.oldest_wait_ms / 1000 % 60));
  if (ImGui::Button("定位最近原字幕")) {
    page = 0;
    stable_only = false;
    search = bridge.queue.last_text;
    search_visible = true;
    transcript_reader.Find(search);
    follow = false;
  }
  ReadPanel(
      "最近一条已确认回复", "reply", bridge.queue.last_reply, reply_reader,
      std::max(120 * s, ImGui::GetContentRegionAvail().y - 32 * s), 2, false);
}
