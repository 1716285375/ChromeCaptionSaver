#include "ChromeCaptionSaver/platform/RelayBridge.h"
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <stdexcept>

namespace ccs::chat {
using json = nlohmann::json;
static int64_t Now() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
RelayBridge::RelayBridge() {
    try {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("本地网页连接服务启动失败。");
        winsock_ = true;
        std::array<unsigned char, 32> random{};
        if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) throw std::runtime_error("无法创建网页配对凭证。");
        constexpr char hex[] = "0123456789abcdef";
        for (auto b : random) { token_ += hex[b >> 4]; token_ += hex[b & 15]; }
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == INVALID_SOCKET) throw std::runtime_error("无法创建本地连接套接字。");
        BOOL exclusive = TRUE; setsockopt(listener_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = 0;
        if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener_, 4)) throw std::runtime_error("无法监听本机网页连接。");
        int length = sizeof(address);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length)) throw std::runtime_error("无法读取本地连接端口。");
        port_ = ntohs(address.sin_port);
        thread_ = std::jthread([this](std::stop_token stop) { Serve(stop); });
    } catch (const std::exception& e) {
        error_ = e.what();
        if (listener_ != INVALID_SOCKET) { closesocket(listener_); listener_ = INVALID_SOCKET; }
    }
}
RelayBridge::~RelayBridge() {
    if (thread_.joinable()) { thread_.request_stop(); thread_.join(); }
    if (listener_ != INVALID_SOCKET) closesocket(listener_);
    if (winsock_) WSACleanup();
}
void RelayBridge::Start(const std::string& instruction, const std::filesystem::path& directory) {
    std::lock_guard lock(mutex_);
    if (!error_.empty()) throw std::runtime_error(error_);
    std::filesystem::create_directories(directory);
    if (journal_.is_open()) journal_.close();
    journal_.clear();
    const auto name = "chatgpt-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".jsonl";
    journal_.open(directory / name, std::ios::binary | std::ios::app);
    if (!journal_) throw std::runtime_error("无法保存 ChatGPT 对话日志。");
    queue_.Start(instruction); client_.clear();
}
void RelayBridge::Stop() { std::lock_guard lock(mutex_); queue_.Stop(); client_.clear(); }
bool RelayBridge::Add(const std::string& sentence) { std::lock_guard lock(mutex_); return queue_.Add(sentence); }
std::string RelayBridge::PendingText() const { std::lock_guard lock(mutex_); return queue_.PendingText(); }
BridgeView RelayBridge::View() const {
    std::lock_guard lock(mutex_);
    const bool connected = heartbeat_ && Now() - heartbeat_ < 6000;
    auto view = queue_.View();
    auto error = error_;
    if (error.empty() && view.enabled && !connected) error = "网页已超过 6 秒没有通信，请检查扩展、刷新 ChatGPT 并重新绑定。";
    return {view, connected, error,heartbeat_?Now()-heartbeat_:-1};
}
std::string RelayBridge::PairCode() const { return error_.empty() ? std::to_string(port_) + ":" + token_ : std::string{}; }
void RelayBridge::Serve(std::stop_token stop) {
    while (!stop.stop_requested()) {
        { std::lock_guard lock(mutex_); queue_.CheckTimeout(Now()); }
        fd_set read; FD_ZERO(&read); FD_SET(listener_, &read);
        timeval timeout{0, 200000};
        if (select(0, &read, nullptr, nullptr, &timeout) <= 0) continue;
        auto client = accept(listener_, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        DWORD timeout_ms = 500;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
        try { Handle(client); } catch (...) { /* Malformed/disconnected clients cannot terminate the bridge. */ }
        closesocket(client);
    }
}
void RelayBridge::Handle(SOCKET client) {
    auto respond = [&](int status, const json& body) {
        const auto payload = body.dump();
        std::string response = "HTTP/1.1 " + std::to_string(status) + (status == 200 ? " OK\r\n" : " Error\r\n");
        response += "Content-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
        size_t sent = 0;
        while (sent < response.size()) { const auto n = send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0); if (n <= 0) break; sent += static_cast<size_t>(n); }
    };
    std::string data;
    std::array<char, 8192> buffer{};
    const auto deadline = Now() + 2500;
    size_t header_end = std::string::npos;
    while ((header_end = data.find("\r\n\r\n")) == std::string::npos && data.size() <= 8192 && Now() < deadline) {
        int n = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0); if (n <= 0) return; data.append(buffer.data(), n);
    }
    if (header_end == std::string::npos || header_end > 8192) { respond(400, {{"error", "headers"}}); return; }
    const auto first = data.find("\r\n");
    std::istringstream request(data.substr(0, first));
    std::string method, path, version; request >> method >> path >> version;
    if (method != "POST") { respond(405, {{"error", "POST required"}}); return; }
    std::string authorization, length_text, host;
    size_t begin = first + 2;
    while (begin < header_end) {
        auto end = data.find("\r\n", begin); auto colon = data.find(':', begin);
        if (colon == std::string::npos || colon >= end) { respond(400, {{"error", "header"}}); return; }
        auto name = data.substr(begin, colon - begin);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        auto value = data.substr(colon + 1, end - colon - 1); while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        if (name == "authorization") authorization = value;
        if (name == "content-length") length_text = value;
        if (name == "host") host = value;
        begin = end + 2;
    }
    if (authorization != "Bearer " + token_ || host != "127.0.0.1:" + std::to_string(port_)) { respond(403, {{"error", "unauthorized"}}); return; }
    if (length_text.empty() || length_text.find_first_not_of("0123456789") != std::string::npos || length_text.size() > 7) { respond(400, {{"error", "length"}}); return; }
    const auto length = std::stoul(length_text);
    if (length > 524288) { respond(413, {{"error", "body too large"}}); return; }
    const size_t body_start = header_end + 4;
    while (data.size() - body_start < length && Now() < deadline) {
        int n = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0); if (n <= 0) return; data.append(buffer.data(), n);
    }
    if (data.size() - body_start != length) { respond(400, {{"error", "incomplete body"}}); return; }
    const auto body = json::parse(data.substr(body_start), nullptr, false);
    if (!body.is_object()) { respond(400, {{"error", "JSON object required"}}); return; }
    std::lock_guard lock(mutex_);
    auto view = queue_.View();
    if (path == "/health") {
        if (body.contains("client") && body["client"].is_string() && body["client"].get<std::string>() == client_ && !client_.empty()) {
            heartbeat_ = Now();
            if (body.contains("id") && body["id"].is_number_unsigned() && body.contains("progress") && body["progress"].is_string())
                queue_.Progress(body["id"].get<uint64_t>(), heartbeat_, body["progress"].get<std::string>());
        }
        respond(200, {{"enabled", view.enabled}, {"epoch", view.epoch}, {"phase", view.phase}}); return;
    }
    for (const char* key : {"client", "url", "reply", "error"})
        if (body.contains(key) && !body[key].is_string()) { respond(400, {{"error", "expected string"}}); return; }
    for (const char* key : {"busy", "draft", "ready"})
        if (body.contains(key) && !body[key].is_boolean()) { respond(400, {{"error", "expected boolean"}}); return; }
    const auto client_id = body.value("client", std::string{});
    const auto url = body.value("url", std::string{});
    if (client_id.empty() || client_id.size() > 128 || !url.starts_with("https://chatgpt.com/")) { respond(403, {{"error", "target"}}); return; }
    if (path == "/poll") {
        if (!view.enabled) client_.clear();
        if (view.enabled && client_.empty()) client_ = client_id;
        if (!client_.empty() && client_ != client_id) { respond(409, {{"error", "another page is bound"}}); return; }
        heartbeat_ = Now();
        const auto task = queue_.Poll(client_id, body.value("busy", false), body.value("draft", false), Now(), body.value("ready", true));
        view = queue_.View();
        json result{{"enabled", view.enabled}, {"epoch", view.epoch}, {"action", task ? "send" : "wait"}, {"phase", view.phase}};
        if (task) { result["id"] = task->id; result["text"] = task->text; result["prompt"] = task->prompt; }
        respond(200, result); return;
    }
    if (client_ != client_id || !body.contains("id") || !body["id"].is_number_unsigned()) { respond(409, {{"error", "unmatched task"}}); return; }
    const auto id = body["id"].get<uint64_t>();
    bool accepted = false;
    if (path == "/sent") accepted = queue_.Sent(id);
    else if (path == "/complete") {
        const auto reply = body.value("reply", std::string{});
        if (reply.size() > 300000) { respond(413, {{"error", "reply too large"}}); return; }
        accepted = queue_.Complete(id, reply);
        if (accepted && queue_.View().completed != view.completed) { journal_ << json{{"id", id}, {"caption", view.last_text}, {"reply", reply}}.dump() << '\n'; journal_.flush(); if (!journal_) queue_.Stop(); }
    } else if (path == "/error") accepted = queue_.Fail(id, body.value("error", std::string("网页操作失败。")));
    else { respond(404, {{"error", "route"}}); return; }
    respond(accepted ? 200 : 409, {{"accepted", accepted}});
}
}
