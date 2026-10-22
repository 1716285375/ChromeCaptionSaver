#pragma once
#include <winsock2.h>
#include "ChromeCaptionSaver/core/ChatQueue.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
namespace ccs::chat {
struct BridgeView { QueueView queue; bool connected = false; std::string error; int64_t since_activity_ms=-1; };
class RelayBridge {
public:
    RelayBridge();
    ~RelayBridge();
    RelayBridge(const RelayBridge&) = delete;
    RelayBridge& operator=(const RelayBridge&) = delete;
    void Start(const std::string& instruction, const std::filesystem::path& log_directory);
    void Stop();
    bool Add(const std::string& sentence);
    BridgeView View() const;
    std::string PairCode() const;
    std::string PendingText() const;
private:
    void Serve(std::stop_token stop);
    void Handle(SOCKET client);
    mutable std::mutex mutex_;
    ChatQueue queue_;
    SOCKET listener_ = INVALID_SOCKET;
    bool winsock_ = false;
    unsigned short port_ = 0;
    std::string token_, client_, error_;
    int64_t heartbeat_ = 0;
    std::ofstream journal_;
    std::jthread thread_;
};
}
