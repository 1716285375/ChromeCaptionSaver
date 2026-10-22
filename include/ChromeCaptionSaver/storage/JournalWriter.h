#pragma once
// Append-only JSONL writer for raw caption snapshots.
#include "ChromeCaptionSaver/core/Types.h"
#include <string>
#include <fstream>
#include <mutex>

namespace ccs::storage {

class JournalWriter {
public:
    JournalWriter() = default;
    ~JournalWriter();

    // Open (create or append) snapshots.jsonl under dir_utf8.
    bool Open(const std::string& dir_utf8);
    void Close();

    // Append one snapshot. Thread-safe.
    bool Append(const core::CaptionSnapshot& snap, uint64_t seq);

    // Flush to disk.
    bool Flush();

private:
    std::ofstream out_;
    std::mutex    mtx_;
};

} // namespace ccs::storage
