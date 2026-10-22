#include <gtest/gtest.h>
#include "ChromeCaptionSaver/storage/JournalWriter.h"
#include "ChromeCaptionSaver/capture/CaptureWorker.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include "ChromeCaptionSaver/core/Paths.h"

class JournalTest : public testing::Test {
protected:
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("ccs-test-" + std::to_string(GetCurrentProcessId()));
    void SetUp() override { std::filesystem::create_directories(dir); }
    void TearDown() override { std::filesystem::remove_all(dir); }
};

TEST_F(JournalTest, UnicodeRoundTripAndAppend) {
    ccs::storage::JournalWriter writer;
    auto unicodeDir = dir / L"字幕";
    auto u8 = unicodeDir.u8string();
    std::string path(u8.begin(), u8.end());
    ASSERT_TRUE(writer.Open(path));
    ccs::core::CaptionSnapshot snap;
    snap.session_id = "session-test";
    snap.joined_utf8 = "你好\nこんにちは \"quoted\"";
    snap.lines_utf8 = {snap.joined_utf8};
    snap.observed_at = ccs::core::TimePoint{};
    ASSERT_TRUE(writer.Append(snap, 1));
    ASSERT_TRUE(writer.Flush());
    writer.Close();
    ASSERT_TRUE(writer.Open(path));
    ASSERT_TRUE(writer.Append(snap, 2));
    writer.Close();
    std::ifstream input(unicodeDir / "snapshots.jsonl");
    std::string line;
    for (int seq = 1; seq <= 2; ++seq) {
        ASSERT_TRUE(static_cast<bool>(std::getline(input, line)));
        auto j = nlohmann::json::parse(line);
        EXPECT_EQ(j["seq"], seq);
        EXPECT_EQ(j["joined"], snap.joined_utf8);
        EXPECT_EQ(j["session_id"], snap.session_id);
        EXPECT_EQ(j["utc"], "1970-01-01T00:00:00Z");
    }
    EXPECT_FALSE(static_cast<bool>(std::getline(input, line)));
}

TEST_F(JournalTest, OpenFailureCanRecover) {
    ccs::storage::JournalWriter writer;
    ccs::core::CaptionSnapshot snap;
    EXPECT_FALSE(writer.Append(snap, 1));
    EXPECT_FALSE(writer.Flush());
    auto file = dir / "file";
    { std::ofstream out(file); out << "preserve"; }
    EXPECT_FALSE(writer.Open(file.string()));
    EXPECT_TRUE(writer.Open(dir.string()));
    EXPECT_TRUE(writer.Append(snap, 1));
}

TEST(CaptureWorker, RejectsInvalidTargetAndInterval) {
    ccs::capture::CaptureWorker worker;
    ccs::capture::CaptureConfig cfg;
    EXPECT_FALSE(worker.Start(nullptr, cfg));
    cfg.poll_interval_ms = 0;
    EXPECT_FALSE(worker.Start(GetDesktopWindow(), cfg));
    worker.Stop();
    EXPECT_EQ(worker.stats().state.load(), ccs::core::CaptureState::Stopped);
}

TEST_F(JournalTest, InvalidUtf8DoesNotCorruptJournal) {
    ccs::storage::JournalWriter writer;
    ASSERT_TRUE(writer.Open(dir.string()));
    ccs::core::CaptionSnapshot snap;
    snap.joined_utf8 = std::string(1, static_cast<char>(0xff));
    EXPECT_FALSE(writer.Append(snap, 1));
    snap.joined_utf8 = "valid";
    EXPECT_TRUE(writer.Append(snap, 2));
    writer.Close();
    std::ifstream input(dir / "snapshots.jsonl");
    std::string line;
    ASSERT_TRUE(static_cast<bool>(std::getline(input, line)));
    EXPECT_EQ(nlohmann::json::parse(line)["joined"], "valid");
    EXPECT_FALSE(static_cast<bool>(std::getline(input, line)));
}

TEST(Paths, SessionDirectoryDoesNotDependOnWorkingDirectory) {
    const auto expected = ccs::core::DefaultSessionsDirectory();
    ASSERT_TRUE(expected.is_absolute());
    EXPECT_EQ(expected.filename(), "sessions");
    EXPECT_EQ(expected.parent_path().filename(), "data");
    const auto original = std::filesystem::current_path();
    struct Restore { std::filesystem::path path; ~Restore() { std::filesystem::current_path(path); } } restore{original};
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    EXPECT_EQ(ccs::core::DefaultSessionsDirectory(), expected);
}
