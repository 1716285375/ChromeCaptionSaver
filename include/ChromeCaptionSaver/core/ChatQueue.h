#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>
namespace ccs::chat {
class SentenceGate {
public:
    std::vector<std::string> Update(const std::string& text, int64_t now_ms, bool finish = false);
private:
    std::vector<std::string> candidates_;
    std::vector<int64_t> since_;
    size_t emitted_ = 0;
};
struct Message { uint64_t id; std::string text, prompt; int64_t queued_at_ms=0; };
struct QueueView {
    bool enabled = false;
    size_t pending = 0;
    uint64_t completed = 0, epoch = 0;
    std::string phase = "未启动", detail, last_text, last_reply;
    size_t in_flight = 0, submitted = 0, failed = 0;
    int64_t oldest_wait_ms = 0;
};
// Single serialized owner (RelayBridge) supplies the synchronization.
class ChatQueue {
public:
    void Start(std::string instruction);
    void Stop();
    bool Add(const std::string& sentence);
    std::optional<Message> Poll(const std::string& client, bool busy, bool draft, int64_t now_ms, bool ready = true);
    bool Sent(uint64_t id);
    bool Progress(uint64_t id, int64_t now_ms, const std::string& status);
    bool Complete(uint64_t id, const std::string& reply);
    bool Fail(uint64_t id, const std::string& error);
    void CheckTimeout(int64_t now_ms);
    QueueView View() const;
    std::string PendingText() const;
private:
    bool enabled_ = false, submitted_ = false;
    uint64_t next_id_ = 1, completed_ = 0, epoch_ = 0, last_completed_id_ = 0;
    int64_t leased_at_ = 0;
    std::string instruction_, client_, phase_ = "未启动", detail_, last_text_, last_reply_;
    std::deque<Message> pending_;
    std::optional<Message> in_flight_;
};
}
