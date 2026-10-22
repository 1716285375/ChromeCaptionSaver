#pragma once
#include "ChromeCaptionSaver/core/Types.h"
#include <filesystem>

namespace ccs::exporter {
// Creates a NEW directory, never overwriting an existing export.
void ExportBundle(const std::vector<core::CaptionSegment>& segments, const std::filesystem::path& directory);
std::vector<core::CaptionSegment> ReadJournal(const std::filesystem::path& file);
std::string Timestamp(int64_t ms, char separator);
}
