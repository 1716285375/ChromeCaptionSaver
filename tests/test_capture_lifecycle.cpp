#include <gtest/gtest.h>
#include "ChromeCaptionSaver/capture/CaptureWorker.h"
#include "ChromeCaptionSaver/platform/UiaClient.h"
#include <future>
#include <functional>
#include "ChromeCaptionSaver/capture/RecordingSession.h"
#include "ChromeCaptionSaver/export/SubtitleExporter.h"
#include <filesystem>

using namespace std::chrono_literals;

// A real HWND/UIA fixture, owned by its own message-pumping thread.
class CaptionWindow {
public:
    HWND root = nullptr;
    HWND label = nullptr;
    std::thread thread;
    explicit CaptionWindow(bool with_label = true) {
        std::promise<std::pair<HWND, HWND>> ready;
        auto future = ready.get_future();
        thread = std::thread([&ready, with_label] {
            WNDCLASSW cls{};
            cls.lpfnWndProc = DefWindowProcW;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpszClassName = L"CaptionBubbleLabel";
            RegisterClassW(&cls);
            auto root = CreateWindowW(L"STATIC", L"CCS test window", WS_OVERLAPPEDWINDOW,
                0, 0, 400, 200, nullptr, nullptr, cls.hInstance, nullptr);
            auto label = with_label ? CreateWindowW(cls.lpszClassName, L"你好 caption", WS_CHILD | WS_VISIBLE,
                0, 0, 300, 100, root, nullptr, cls.hInstance, nullptr) : nullptr;
            ShowWindow(root, SW_SHOWNOACTIVATE);
            ready.set_value({root, label});
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            DestroyWindow(root);
        });
        auto handles = future.get();
        root = handles.first;
        label = handles.second;
    }
    ~CaptionWindow() {
        PostThreadMessageW(GetThreadId(thread.native_handle()), WM_QUIT, 0, 0);
        thread.join();
    }
};

static bool WaitFor(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

TEST(UiaClient, CaptionScopeAndSilentInterval) {
    CaptionWindow window;
    ASSERT_NE(window.label, nullptr);
    ccs::platform::UiaClient uia;
    ASSERT_TRUE(uia.Initialize());
    ASSERT_TRUE(uia.Initialize());
    auto root = uia.ElementFromHandle(window.root);
    ASSERT_TRUE(root);
    std::vector<std::wstring> lines;
    std::wstring method;
    ASSERT_TRUE(uia.ReadCaptionText(root.Get(), lines, method));
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], L"你好 caption");
    SetWindowTextW(window.label, L"");
    EXPECT_TRUE(uia.ReadCaptionText(root.Get(), lines, method));
    EXPECT_TRUE(lines.empty());
    // A sibling ordinary text element must never become a caption source.
    CaptionWindow ordinary(false);
    auto element = uia.ElementFromHandle(ordinary.root);
    ASSERT_TRUE(element);
    EXPECT_FALSE(uia.ReadCaptionText(element.Get(), lines, method));
    EXPECT_TRUE(lines.empty());
}

TEST(UiaClient, DoesNotUninitializeCallerSta) {
    ASSERT_EQ(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), S_OK);
    {
        ccs::platform::UiaClient uia;
        ASSERT_TRUE(uia.Initialize());
        uia.Shutdown();
    }
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    EXPECT_EQ(hr, S_FALSE);
    if (SUCCEEDED(hr)) CoUninitialize();
    CoUninitialize();
}

TEST(CaptureWorker, PauseResumeRestartAndTargetLoss) {
    CaptionWindow window;
    ASSERT_NE(window.label, nullptr);
    ccs::capture::CaptureConfig cfg;
    cfg.poll_interval_ms = 30;
    cfg.session_id = "first";
    ccs::capture::CaptureWorker worker;
    ASSERT_TRUE(worker.Start(window.root, cfg));
    EXPECT_FALSE(worker.Start(window.root, cfg));
    ASSERT_TRUE(WaitFor([&] { return worker.stats().snapshots_written > 0; }));
    worker.Pause();
    ASSERT_EQ(worker.stats().state.load(), ccs::core::CaptureState::Paused);
    // Allow an already in-flight UIA call to finish before measuring paused polls.
    std::this_thread::sleep_for(100ms);
    auto polls = worker.stats().poll_cycles.load();
    SetWindowTextW(window.label, L"resumed caption");
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(worker.stats().poll_cycles.load(), polls);
    worker.Resume();
    ASSERT_TRUE(WaitFor([&] { return worker.stats().snapshots_written >= 2; }));
    worker.Stop();
    std::vector<ccs::core::CaptionSnapshot> batch;
    ASSERT_TRUE(worker.Drain(batch));
    EXPECT_EQ(batch.back().joined_utf8, "resumed caption");
    EXPECT_EQ(batch.back().process_id, GetCurrentProcessId());
    EXPECT_EQ(batch.back().session_id, "first");
    cfg.session_id = "second";
    ASSERT_TRUE(worker.Start(window.root, cfg));
    ASSERT_TRUE(WaitFor([&] { return worker.stats().snapshots_written > 0; }));
    SetWindowTextW(window.label, L"changed after restart");
    ASSERT_TRUE(WaitFor([&] { return worker.stats().snapshots_written >= 2; }));
    // Removing just the label must fail, even while the target HWND still exists.
    SendMessageW(window.label, WM_CLOSE, 0, 0);
    ASSERT_TRUE(WaitFor([&] { return worker.stats().state == ccs::core::CaptureState::TargetNotFound; }));
    worker.Stop();
    EXPECT_EQ(worker.stats().state.load(), ccs::core::CaptureState::TargetNotFound);
    batch.clear();
    ASSERT_TRUE(worker.Drain(batch));
    EXPECT_EQ(batch.front().session_id, "second");
    EXPECT_EQ(batch.front().joined_utf8, "resumed caption");
}

TEST(CaptureWorker, StopInterruptsLongPollAndPause) {
    CaptionWindow window;
    ASSERT_NE(window.label, nullptr);
    ccs::capture::CaptureConfig cfg;
    cfg.poll_interval_ms = 60000;
    ccs::capture::CaptureWorker worker;
    for (bool pause : {false, true}) {
        ASSERT_TRUE(worker.Start(window.root, cfg));
        ASSERT_TRUE(WaitFor([&] { return worker.stats().snapshots_written > 0; }));
        if (pause) worker.Pause();
        auto start = std::chrono::steady_clock::now();
        worker.Stop();
        EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
    }
}

TEST(RecordingSession, PersistsPauseBoundaryAndExportsInBackground) {
    CaptionWindow window;
    const auto root = std::filesystem::temp_directory_path() / ("ccs-session-" + std::to_string(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    ccs::capture::RecordingSession session;
    ASSERT_TRUE(session.Start(window.root, root));
    ASSERT_TRUE(WaitFor([&] { return session.View().snapshots >= 1; }));
    EXPECT_FALSE(session.Start(window.root, root));
    session.Pause(true);
    ASSERT_TRUE(WaitFor([&] { return session.View().state == ccs::core::CaptureState::Paused; }));
    const auto paused = session.View().snapshots;
    SetWindowTextW(window.label, L"new after pause");
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(session.View().snapshots, paused);
    session.Pause(false);
    ASSERT_TRUE(WaitFor([&] { return session.View().snapshots > paused; }));
    session.Export();
    ASSERT_TRUE(WaitFor([&] { return !session.View().last_export.empty(); }));
    const auto early = session.View().last_export;
    EXPECT_TRUE(std::filesystem::exists(early / "captions.approx.srt"));
    session.Stop();
    ASSERT_TRUE(WaitFor([&] { return !session.View().running; }));
    const auto final = session.View();
    EXPECT_NE(final.transcript_text.find("new after pause"), std::string::npos);
    EXPECT_FALSE(session.DrainSentences().empty());
    EXPECT_TRUE(session.DrainSentences().empty());
    EXPECT_NE(final.last_export, early);
    EXPECT_TRUE(std::filesystem::exists(final.last_export / "captions.approx.vtt"));
    ccs::capture::SessionView cached;session.UpdateView(cached);
    EXPECT_EQ(cached.transcript_text,final.transcript_text);
    EXPECT_NE(cached.stable_text.find("你好 caption"),std::string::npos);
    EXPECT_GT(cached.text_revision,0u);
    auto* text_storage=cached.transcript_text.data();session.UpdateView(cached);
    EXPECT_EQ(cached.transcript_text.data(),text_storage);
    const auto segments = ccs::exporter::ReadJournal(final.directory / "snapshots.jsonl");
    ASSERT_EQ(segments.size(), 2u);
    EXPECT_EQ(segments[0].text_utf8, "你好 caption");
    EXPECT_EQ(segments[1].text_utf8, "new after pause");
}
