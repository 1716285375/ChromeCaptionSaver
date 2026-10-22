#include "ChromeCaptionSaver/platform/RelayBridge.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
using nlohmann::json;
static std::pair<int,json> Request(const std::string& code,const std::string& route,json body,bool authorized=true) {
    auto colon=code.find(':'); auto port=static_cast<unsigned short>(std::stoi(code.substr(0,colon)));
    SOCKET socket_handle=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(socket_handle==INVALID_SOCKET) throw std::runtime_error("socket");
    struct Close {SOCKET s;~Close(){closesocket(s);}} close{socket_handle};
    DWORD timeout=3000;setsockopt(socket_handle,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
    sockaddr_in target{};target.sin_family=AF_INET;target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);target.sin_port=htons(port);
    if(connect(socket_handle,reinterpret_cast<sockaddr*>(&target),sizeof(target)))throw std::runtime_error("connect");
    const auto content=body.dump();
    std::string request="POST "+route+" HTTP/1.1\r\nHost: 127.0.0.1:"+std::to_string(port)+"\r\n";
    if(authorized)request+="Authorization: Bearer "+code.substr(colon+1)+"\r\n";
    request+="Content-Type: application/json\r\nContent-Length: "+std::to_string(content.size())+"\r\n\r\n"+content;
    for(size_t at=0;at<request.size();) {int n=send(socket_handle,request.data()+at,static_cast<int>(request.size()-at),0);if(n<=0)throw std::runtime_error("send");at+=n;}
    std::string response;char buffer[4096];int n;
    while((n=recv(socket_handle,buffer,sizeof(buffer),0))>0)response.append(buffer,n);
    auto split=response.find("\r\n\r\n");if(split==std::string::npos)throw std::runtime_error("response");
    return {std::stoi(response.substr(9,3)),json::parse(response.substr(split+4))};
}
TEST(RelayBridge, AuthenticatedSerialRoundTripAndStop) {
    ccs::chat::RelayBridge bridge;auto code=bridge.PairCode();ASSERT_FALSE(code.empty());
    EXPECT_EQ(Request(code,"/health",json::object(),false).first,403);
    auto folder=std::filesystem::temp_directory_path()/std::filesystem::path("ccs-relay-test-"+std::to_string(GetCurrentProcessId()));
    bridge.Start("Translate",folder);ASSERT_TRUE(bridge.Add("One."));ASSERT_TRUE(bridge.Add("Two."));
    json poll{{"client","test"},{"url","https://chatgpt.com/c/test"},{"busy",false},{"draft",false}};
    auto bad=poll;bad["url"]="https://evil.example/";EXPECT_EQ(Request(code,"/poll",bad).first,403);
    bad=poll;bad["busy"]="wrong";EXPECT_EQ(Request(code,"/poll",bad).first,400);
    auto first=Request(code,"/poll",poll);ASSERT_EQ(first.first,200);ASSERT_EQ(first.second["action"],"send");
    EXPECT_EQ(Request(code,"/poll",poll).second["action"],"wait");
    auto ack=poll;ack["id"]=first.second["id"];ack["reply"]="一";
    EXPECT_EQ(Request(code,"/complete",ack).first,409);
    EXPECT_EQ(Request(code,"/sent",ack).first,200);
    for(int i=0;i<300;++i) ASSERT_TRUE(bridge.Add("Backlog."));
    EXPECT_TRUE(Request(code,"/health",poll).second["enabled"].get<bool>());
    EXPECT_EQ(Request(code,"/complete",ack).first,200);
    EXPECT_EQ(Request(code,"/complete",ack).first,200);
    EXPECT_EQ(bridge.View().queue.completed,1u);EXPECT_EQ(Request(code,"/poll",poll).second["text"],"Two.");
    bridge.Stop();EXPECT_FALSE(Request(code,"/health",json::object()).second["enabled"].get<bool>());
    EXPECT_EQ(Request(code,"/complete",ack).first,409);
    poll["client"]="refreshed-page";
    EXPECT_EQ(Request(code,"/poll",poll).first,200);
    bridge.Start("Translate",folder);bridge.Add("Restart.");
    poll["ready"]=false;
    EXPECT_EQ(Request(code,"/poll",poll).second["action"],"wait");
    EXPECT_NE(bridge.View().queue.phase.find("输入框"),std::string::npos);
    poll["ready"]=true;
    EXPECT_EQ(Request(code,"/poll",poll).second["action"],"send");
    bridge.Stop();
    // Test files are retained for inspection under the OS temporary directory.
}
TEST(RelayBridge, HundredSequentialRepliesWithRepeatedAcknowledgements) {
    ccs::chat::RelayBridge bridge;auto code=bridge.PairCode();ASSERT_FALSE(code.empty());
    const auto folder=std::filesystem::temp_directory_path()/("ccs-relay-endurance-"+std::to_string(GetCurrentProcessId()));
    bridge.Start("Translate",folder);
    for(int i=0;i<100;++i)ASSERT_TRUE(bridge.Add("Sentence "+std::to_string(i)));
    json poll{{"client","endurance"},{"url","https://chatgpt.com/c/test"},{"busy",false},{"draft",false}};
    for(int i=0;i<100;++i) {
        auto task=Request(code,"/poll",poll); ASSERT_EQ(task.second["action"],"send");
        auto ack=poll;ack["id"]=task.second["id"];ack["reply"]="Reply "+std::to_string(i);
        ASSERT_EQ(Request(code,"/sent",ack).first,200);
        auto health=ack;health["progress"]="Waiting";
        ASSERT_EQ(Request(code,"/health",health).first,200);
        ASSERT_EQ(bridge.View().queue.detail,"Waiting");
        ASSERT_EQ(Request(code,"/complete",ack).first,200);
        ASSERT_EQ(Request(code,"/complete",ack).first,200);
        ASSERT_EQ(bridge.View().queue.completed,static_cast<uint64_t>(i+1));
    }
    ASSERT_EQ(bridge.View().queue.pending,0u);bridge.Stop();
    size_t lines=0;for(const auto& entry:std::filesystem::directory_iterator(folder)) {
        std::ifstream input(entry.path());std::string line;while(std::getline(input,line))++lines;
    }
    EXPECT_EQ(lines,100u);
}
