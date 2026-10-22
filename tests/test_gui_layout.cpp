#include "TranscriptView.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ui/HistoryPreview.h"
#include "ui/Motion.h"
#include "ui/Theme.h"
#include <cstdio>
#include <string>
#include <thread>

static int errors = 0;
static bool caught_cursor_error = false;
static std::string clipboard;
static void FrameStart(float width, float height) {
  ImGui::GetIO().DisplaySize = {width, height};
  ImGui::NewFrame();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::Begin("ChromeCaptionSaver/content", nullptr,
               ImGuiWindowFlags_NoSavedSettings);
}
int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DeltaTime = 1.0f / 60;
  io.ConfigErrorRecoveryEnableAssert = false;
  io.ConfigErrorRecoveryEnableTooltip = false;
  io.ConfigErrorRecoveryEnableDebugLog = false;
  ImGui::GetCurrentContext()->ErrorCallback = [](ImGuiContext *, void *,
                                                 const char *message) {
    ++errors;
    if (std::string(message).find("SetCursorPos") != std::string::npos)
      caught_cursor_error = true;
  };
  io.Fonts->AddFontDefault();
  io.Fonts->Build();
  // Negative control: reproduce the reported API violation so this detector
  // cannot silently pass.
  FrameStart(800, 600);
  ImGui::BeginChild("transcript", {400, 200});
  ImGui::TextUnformatted("caption");
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 100);
  ImGui::EndChild();
  ImGui::End();
  ImGui::Render();
  if (!caught_cursor_error) {
    std::fprintf(stderr,
                 "Failed to detect original SetCursorPos layout error\n");
    return 1;
  }
  errors = 0;
  SelectableTextView reader;
  for (int count : {0, 1, 200})
    for (bool follow : {false, true})
      for (float scale : {1.0f, 1.5f}) {
        std::vector<ccs::core::CaptionSegment> segments(
            static_cast<size_t>(count));
        for (auto &segment : segments) {
          segment.text_utf8 = "Long caption line testing real row bounds and "
                              "list clipping, with UTF-8 中文日本語.";
          segment.quality = "tentative";
        }
        // Two frames exercise both clipper measurement and follow-scroll
        // position.
        for (int frame = 0; frame < 2; ++frame) {
          FrameStart(800 * scale, 600 * scale);
          DrawTranscriptList(segments, follow, scale, reader);
          ImGui::End();
          ImGui::Render();
        }
      }
  ImGui::GetPlatformIO().Platform_SetClipboardTextFn =
      [](ImGuiContext *, const char *text) { clipboard = text; };
  ImGui::GetPlatformIO().Platform_GetClipboardTextFn = [](ImGuiContext *) {
    return clipboard.c_str();
  };
  SelectableTextView selectable;
  auto draw = [&](const std::string &text) {
    FrameStart(800, 600);
    selectable.Draw("selectable", text, {600, 300}, false);
    ImGui::End();
    ImGui::Render();
  };
  draw("Alpha beta gamma");
  selectable.select_all = true;
  draw("Alpha beta gamma");
  draw("Alpha beta gamma");
  if (selectable.selection_start != 0 || selectable.selection_end != 16)
    return 3;
  draw("Arriving text must not disturb selection");
  if (selectable.source != "Alpha beta gamma")
    return 4;
  io.AddKeyEvent(ImGuiKey_End, true);
  draw("Alpha beta gamma");
  io.AddKeyEvent(ImGuiKey_End, false);
  draw("Alpha beta gamma");
  if (selectable.selection_start != selectable.selection_end)
    return 6;
  io.AddKeyEvent(ImGuiMod_Ctrl, true);
  io.AddKeyEvent(ImGuiKey_A, true);
  draw("Alpha beta gamma");
  if (selectable.selection_start != 0 || selectable.selection_end != 16)
    return 7;
  io.AddKeyEvent(ImGuiKey_A, false);
  io.AddKeyEvent(ImGuiKey_C, true);
  draw("Alpha beta gamma");
  if (clipboard != "Alpha beta gamma") {
    std::fprintf(stderr, "Ctrl+A/C clipboard mismatch\n");
    return 5;
  }
  io.AddKeyEvent(ImGuiKey_C, false);
  io.AddKeyEvent(ImGuiMod_Ctrl, false);
  selectable.Resume();
  draw("Search 中文日本語 target");
  selectable.Find("日本語");
  draw("Search 中文日本語 target");
  draw("Search 中文日本語 target");
  if (selectable.search_status != "已定位" ||
      selectable.selection_end <= selectable.selection_start)
    return 8;
  selectable.Find("TARGET");
  draw("Search 中文日本語 target");
  draw("Search 中文日本語 target");
  if (selectable.search_status != "已定位")
    return 13;
  selectable.Find("missing");
  draw("Search 中文日本語 target");
  draw("Search 中文日本語 target");
  if (selectable.search_status != "未找到")
    return 9;
  for (float scale : {1.f, 1.25f, 1.5f})
    for (float width : {1280.f, 1440.f, 1600.f, 1920.f}) {
      auto layout = ccs::ui::Layout::For(width * scale, 720 * scale, scale);
      if (layout.sidebar != (width >= 1440 ? 220.f : 70.f) ||
          layout.current != 150.f)
        return 10;
    }
  float a = 0, b = 0;
  for (int i = 0; i < 60; ++i)
    a = ccs::ui::Approach(a, 1, 1.f / 60);
  for (int i = 0; i < 30; ++i)
    b = ccs::ui::Approach(b, 1, 1.f / 30);
  if (std::abs(a - b) > .001f)
    return 11;
  selectable.Resume();
  std::string long_text;
  for (int i = 0; i < 10000; ++i)
    long_text += "Long transcript, original selectable text, row " +
                 std::to_string(i) + ".\n";
  for (int i = 0; i < 3; ++i) {
    FrameStart(1600, 900);
    selectable.Draw("long-reader", long_text, {1450, 700}, false);
    ImGui::End();
    ImGui::Render();
    if (ImGui::GetDrawData()->TotalVtxCount > 120000) {
      std::fprintf(stderr, "Long transcript failed to clip offscreen glyphs\n");
      return 12;
    }
  }
  for (float available : {500.f, 800.f, 1400.f}) {
    if (ccs::ui::HistoryLayout::Clamp(-100, available) != 160)
      return 14;
    if (ccs::ui::HistoryLayout::Clamp(10000, available) > available - 300)
      return 15;
  }
  ccs::ui::HistoryPreview history;
  std::promise<void> release;
  auto released = release.get_future().share();
  history.Select("first", [released](const auto &) {
    released.wait();
    return "old contents";
  });
  history.Select("second", [](const auto &) { return "selected contents"; });
  if (!history.text.empty() || !history.loading)
    return 16;
  release.set_value();
  auto wait_history = [&] {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (history.loading && std::chrono::steady_clock::now() < deadline) {
      history.Poll();
      if (history.text == "old contents")
        return false;
      std::this_thread::yield();
    }
    return !history.loading;
  };
  if (!wait_history() || history.text != "selected contents")
    return 17;
  history.Select("broken", [](const auto &) -> std::string {
    throw std::runtime_error("read failed");
  });
  if (!wait_history() || history.error != "read failed" ||
      !history.text.empty())
    return 18;
  history.Select("empty", [](const auto &) { return std::string{}; });
  if (!wait_history() || !history.error.empty() || !history.text.empty())
    return 19;
  ImGui::DestroyContext();
  if (errors) {
    std::fprintf(stderr, "Transcript layout emitted %d ImGui errors\n", errors);
    return 2;
  }
  std::puts("Original SetCursorPos error reproduced; real transcript view "
            "passed 24 render frames without errors.");
  return 0;
}
