#include <gtest/gtest.h>
#include "ChromeCaptionSaver/transcript/TranscriptAssembler.h"
#include "ChromeCaptionSaver/export/SubtitleExporter.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <windows.h>

using ccs::transcript::TranscriptAssembler;
namespace fs = std::filesystem;
static void Add(TranscriptAssembler& a, const std::string& text, int64_t ms, uint64_t seq) {
    ccs::core::CaptionSnapshot snapshot;
    snapshot.joined_utf8 = text; snapshot.t_ms = ms; a.Add(snapshot, seq);
}
TEST(Transcript, GrowthAndLocalRevision) {
    TranscriptAssembler a;
    Add(a, "I can", 0, 1); Add(a, "I can see", 100, 2); Add(a, "I can't see", 200, 3);
    a.Finish(1000);
    ASSERT_EQ(a.segments().size(), 1u);
    EXPECT_EQ(a.segments()[0].text_utf8, "I can't see");
    EXPECT_EQ(a.segments()[0].first_snapshot_seq, 1u);
    EXPECT_EQ(a.segments()[0].last_snapshot_seq, 3u);
}
TEST(Transcript, RollingOverlapAndUnicodeBoundaries) {
    TranscriptAssembler a;
    Add(a, "今天下雨", 0, 1); Add(a, "下雨出门", 500, 2); a.Finish(1000);
    ASSERT_EQ(a.segments().size(), 2u);
    EXPECT_EQ(a.segments()[0].text_utf8, "今天");
    EXPECT_EQ(a.segments()[1].text_utf8, "下雨出门");
    EXPECT_EQ(a.segments()[0].quality, "stable");
    EXPECT_LE(a.segments()[0].end_ms, a.segments()[1].start_ms);
}
TEST(Transcript, RepeatedSpeechAfterSilenceIsPreserved) {
    TranscriptAssembler a;
    Add(a, "hello", 0, 1); Add(a, "hello", 100, 2); Add(a, "", 200, 3);
    Add(a, "hello", 300, 4); a.Finish(500);
    ASSERT_EQ(a.segments().size(), 2u);
    EXPECT_EQ(a.segments()[0].text_utf8, a.segments()[1].text_utf8);
}
TEST(Transcript, ResetAndWhitespaceAndPreview) {
    TranscriptAssembler a;
    Add(a, "  alpha\n beta\t ", 0, 1); Add(a, "new unrelated", 300, 2);
    auto preview = a.Preview(400);
    ASSERT_EQ(preview.size(), 2u);
    EXPECT_EQ(preview[0].text_utf8, "alpha beta");
    EXPECT_EQ(preview[0].quality, "ambiguous");
    EXPECT_EQ(a.segments().size(), 1u);
    EXPECT_THROW(Add(a, "bad", 200, 3), std::invalid_argument);
}
class ExportTest : public testing::Test {
protected:
    fs::path root = fs::temp_directory_path() / ("ccs-export-" + std::to_string(GetCurrentProcessId()));
    void SetUp() override { fs::create_directories(root); }
    void TearDown() override { fs::remove_all(root); }
    static std::string Read(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
};
TEST_F(ExportTest, ValidBundleAndNoOverwrite) {
    TranscriptAssembler a;
    Add(a, "你好 <tag> & 日本語", 100, 1); a.Finish(2000);
    const auto dir = root / L"字幕导出";
    ccs::exporter::ExportBundle(a.segments(), dir);
    EXPECT_EQ(Read(dir / "transcript.txt"), "你好 <tag> & 日本語\n");
    auto srt = Read(dir / "captions.approx.srt");
    EXPECT_NE(srt.find("1\n00:00:00,100 --> 00:00:02,000\n"), std::string::npos);
    EXPECT_NE(srt.find("&lt;tag&gt; &amp;"), std::string::npos);
    EXPECT_TRUE(Read(dir / "captions.approx.vtt").starts_with("WEBVTT\n\nNOTE"));
    EXPECT_EQ(nlohmann::json::parse(Read(dir / "segments.jsonl"))["text"], "你好 <tag> & 日本語");
    EXPECT_THROW(ccs::exporter::ExportBundle(a.segments(), dir), std::runtime_error);
    EXPECT_EQ(Read(dir / "captions.approx.srt"), srt);
    EXPECT_EQ(ccs::exporter::Timestamp(90061001, ','), "25:01:01,001");
}
TEST_F(ExportTest, LongUnicodeAndInvalidTiming) {
    ccs::core::CaptionSegment segment;
    segment.id = 1; segment.start_ms = 0; segment.end_ms = 3000;
    for (int i = 0; i < 180; ++i) segment.text_utf8 += "字";
    ccs::exporter::ExportBundle({segment}, root / "long");
    const auto srt = Read(root / "long/captions.approx.srt");
    EXPECT_NE(srt.find("3\n00:00:02,000 --> 00:00:03,000"), std::string::npos);
    EXPECT_NO_THROW(nlohmann::json(srt).dump());
    segment.end_ms = 0;
    EXPECT_THROW(ccs::exporter::ExportBundle({segment}, root / "bad"), std::invalid_argument);
    EXPECT_FALSE(fs::exists(root / "bad"));
    EXPECT_THROW(ccs::exporter::ExportBundle({}, root / "empty"), std::invalid_argument);
}
TEST_F(ExportTest, JournalSessionsAndMalformedLine) {
    auto path = root / "snapshots.jsonl";
    { std::ofstream out(path); out << R"({"session_id":"a","seq":1,"t_ms":0,"joined":"first"})" << '\n'
        << R"({"session_id":"b","seq":1,"t_ms":0,"joined":"second"})" << '\n'; }
    auto segments = ccs::exporter::ReadJournal(path);
    ASSERT_EQ(segments.size(), 2u);
    EXPECT_EQ(segments[0].text_utf8, "first");
    EXPECT_EQ(segments[1].text_utf8, "second");
    EXPECT_LE(segments[0].end_ms, segments[1].start_ms);
    { std::ofstream out(path, std::ios::app); out << "{incomplete"; }
    try { ccs::exporter::ReadJournal(path); FAIL() << "Expected malformed line rejection"; }
    catch (const std::runtime_error& e) { EXPECT_NE(std::string(e.what()).find("Journal line 3"), std::string::npos); }
}
