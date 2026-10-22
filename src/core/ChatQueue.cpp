#include "ChromeCaptionSaver/core/ChatQueue.h"
#include <stdexcept>
#include <cctype>
#include <algorithm>
#include <chrono>
namespace ccs::chat {
static int64_t QueueNow(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static std::string Trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
static size_t Punctuation(const std::string& text, size_t at) {
    if (text[at] == '.' || text[at] == '?' || text[at] == '!') {
        if (text[at] == '.' && at > 0 && at + 1 < text.size() && isdigit(static_cast<unsigned char>(text[at-1])) && isdigit(static_cast<unsigned char>(text[at+1]))) return 0;
        return 1;
    }
    for (const char* p : {"。", "！", "？"}) if (text.compare(at, 3, p) == 0) return 3;
    return 0;
}
std::vector<std::string> SentenceGate::Update(const std::string& text, int64_t now, bool finish) {
    std::vector<std::string> next;
    size_t begin = 0;
    for (size_t i = 0; i < text.size(); ++i) if (auto n = Punctuation(text, i)) {
        size_t end = i + n;
        while (end < text.size()) { auto more = Punctuation(text, end); if (!more) break; end += more; }
        auto sentence = Trim(text.substr(begin, end - begin));
        if (!sentence.empty()) next.push_back(std::move(sentence));
        begin = end; i = end - 1;
    }
    if (finish) { auto tail = Trim(text.substr(begin)); if (!tail.empty()) next.push_back(std::move(tail)); }
    std::vector<int64_t> times(next.size(), now);
    for (size_t i = 0; i < next.size() && i < candidates_.size(); ++i) if (next[i] == candidates_[i]) times[i] = since_[i];
    candidates_ = std::move(next); since_ = std::move(times);
    std::vector<std::string> result;
    while (emitted_ < candidates_.size() && (finish || now - since_[emitted_] >= 1000)) result.push_back(candidates_[emitted_++]);
    return result;
}
void ChatQueue::Start(std::string instruction) {
    if (instruction.empty() || instruction.size() > 12000) throw std::invalid_argument("提示词长度不合法。");
    instruction_ = std::move(instruction); pending_.clear(); in_flight_.reset(); client_.clear();
    completed_ = 0; last_completed_id_ = 0; submitted_ = false; enabled_ = true; ++epoch_; phase_ = "等待字幕"; detail_.clear(); last_reply_.clear(); last_text_.clear();
}
void ChatQueue::Stop() {
    enabled_ = false; pending_.clear(); in_flight_.reset(); submitted_ = false; ++epoch_;
    phase_ = "已停止"; detail_ = "不再发送新消息；网页已经提交的回答可能仍在生成。";
}
bool ChatQueue::Add(const std::string& sentence) {
    if (!enabled_ || sentence.empty()) return false;
    if (sentence.size() > 12000) { detail_ = "一条字幕超过 12000 字节，未入队；原始录制仍保留，当前回答和其余队列继续处理。"; return false; }
    pending_.push_back({next_id_++, sentence, instruction_ + "\n\n" + sentence,QueueNow()});
    return true;
}
std::optional<Message> ChatQueue::Poll(const std::string& client, bool busy, bool draft, int64_t now, bool ready) {
    CheckTimeout(now);
    if (!enabled_) return {};
    if (client_.empty()) client_ = client;
    if (client_ != client) return {};
    if (in_flight_) return {};
    if (!ready) { phase_ = "找不到网页输入框，请登录并刷新 ChatGPT"; return {}; }
    if (draft) { phase_ = "等待清空网页草稿"; return {}; }
    if (busy) { phase_ = "等待网页回答"; return {}; }
    if (pending_.empty()) { phase_ = "等待字幕"; return {}; }
    in_flight_ = std::move(pending_.front()); pending_.pop_front(); leased_at_ = now; submitted_ = false;
    phase_ = "正在提交"; last_text_ = in_flight_->text;
    return in_flight_;
}
bool ChatQueue::Sent(uint64_t id) {
    if (enabled_ && id == last_completed_id_) return true;
    if (!in_flight_ || in_flight_->id != id || !enabled_) return false;
    submitted_ = true; phase_ = "等待 ChatGPT 回答"; return true;
}
bool ChatQueue::Progress(uint64_t id, int64_t now, const std::string& status) {
    if (!enabled_ || !in_flight_ || in_flight_->id != id || status.size() > 512) return false;
    leased_at_ = now; detail_ = status; return true;
}
bool ChatQueue::Complete(uint64_t id, const std::string& reply) {
    if (enabled_ && id == last_completed_id_ && !reply.empty() && reply == last_reply_) return true;
    if (!enabled_ || !submitted_ || !in_flight_ || in_flight_->id != id || reply.empty()) return false;
    last_reply_ = reply; last_completed_id_ = id; ++completed_; in_flight_.reset(); submitted_ = false; phase_ = "等待下一句"; detail_.clear(); return true;
}
bool ChatQueue::Fail(uint64_t id, const std::string& error) {
    if (!in_flight_ || in_flight_->id != id) return false;
    enabled_ = false; phase_ = "联动已暂停"; detail_ = error + " 不会自动重发，请检查网页后重启联动。"; return true;
}
void ChatQueue::CheckTimeout(int64_t now) {
    if (enabled_ && in_flight_ && now - leased_at_ > 180000) Fail(in_flight_->id, "网页响应超过 180 秒，任务状态未确认。");
}
QueueView ChatQueue::View() const {
    auto detail = detail_;
    if (enabled_ && pending_.size() >= 256) detail += " 网页处理慢于字幕输入，待发送内容继续保留；当前回答完成后按顺序发送。";
    QueueView view{enabled_, pending_.size(), completed_, epoch_, phase_, detail, last_text_, last_reply_};
    view.in_flight=in_flight_?1:0;view.submitted=in_flight_&&submitted_?1:0;view.failed=in_flight_&&!enabled_?1:0;
    view.oldest_wait_ms=pending_.empty()?0:std::max<int64_t>(0,QueueNow()-pending_.front().queued_at_ms);
    return view;
}
std::string ChatQueue::PendingText() const {
    std::string text;
    if(in_flight_)text="[已领取，提交状态请核对网页]\n"+in_flight_->text+"\n\n";
    for(const auto& item:pending_)text+=item.text+"\n\n";
    return text;
}
}
