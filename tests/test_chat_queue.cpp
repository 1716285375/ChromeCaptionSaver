#include "ChromeCaptionSaver/core/ChatQueue.h"
#include <gtest/gtest.h>
using namespace ccs::chat;
TEST(Sentences, StablePrefixAndFinalTail) {
    SentenceGate gate;
    EXPECT_TRUE(gate.Update("Price 3.14. 下一句", 0).empty());
    EXPECT_EQ(gate.Update("Price 3.14. 下一句增加", 1100), std::vector<std::string>{"Price 3.14."});
    EXPECT_EQ(gate.Update("Price 3.14. 下一句增加", 1200, true), std::vector<std::string>{"下一句增加"});
}
TEST(Sentences, RepetitionsAndCorrections) {
    SentenceGate gate;
    gate.Update("你好。你好。", 0);
    EXPECT_EQ(gate.Update("您好。你好。", 1100).size(), 0u);
    EXPECT_EQ(gate.Update("您好。你好。", 2200).size(), 2u);
    EXPECT_TRUE(gate.Update("您好。你好。", 4000).empty());
}
TEST(ChatQueue, SerialAcknowledgementsAndPageOwnership) {
    ChatQueue q; q.Start("Translate"); q.Add("One."); q.Add("Two.");
    EXPECT_FALSE(q.Poll("a", true, false, 0));
    EXPECT_FALSE(q.Poll("a", false, false, 0, false));
    EXPECT_NE(q.View().phase.find("输入框"),std::string::npos);
    EXPECT_FALSE(q.Poll("a", false, true, 0));
    EXPECT_FALSE(q.Poll("b", false, false, 0));
    auto first=q.Poll("a", false, false, 0); ASSERT_TRUE(first);
    EXPECT_EQ(first->prompt,"Translate\n\nOne.");
    EXPECT_FALSE(q.Complete(first->id,"premature"));
    EXPECT_FALSE(q.Poll("a",false,false,1));
    EXPECT_FALSE(q.Sent(first->id+1)); EXPECT_TRUE(q.Sent(first->id));
    EXPECT_TRUE(q.Complete(first->id,"译文"));
    EXPECT_TRUE(q.Poll("a",false,false,2)); EXPECT_EQ(q.View().completed,1u);
    q.Stop(); EXPECT_FALSE(q.Poll("a",false,false,3)); EXPECT_EQ(q.View().pending,0u);
}
TEST(ChatQueue, TimeoutNeverRetries) {
    ChatQueue q; q.Start("Translate"); q.Add("One.");
    auto task=q.Poll("a",false,false,0); ASSERT_TRUE(task);
    q.CheckTimeout(180001); EXPECT_FALSE(q.View().enabled); EXPECT_FALSE(q.Poll("a",false,false,180002));
}
TEST(ChatQueue, BacklogCannotCancelSubmittedReply) {
    ChatQueue q; q.Start("Translate"); ASSERT_TRUE(q.Add("First."));
    auto first=q.Poll("a",false,false,0); ASSERT_TRUE(first); ASSERT_TRUE(q.Sent(first->id));
    for(int i=0;i<300;++i) EXPECT_TRUE(q.Add("Next."));
    EXPECT_TRUE(q.View().enabled);
    EXPECT_TRUE(q.Complete(first->id,"First reply"));
    EXPECT_EQ(q.View().last_reply,"First reply");
    EXPECT_EQ(q.View().pending,300u);
    auto second=q.Poll("a",false,false,1); ASSERT_TRUE(second);
    ASSERT_TRUE(q.Sent(second->id)); EXPECT_TRUE(q.Complete(second->id,"Next reply"));
    EXPECT_EQ(q.View().completed,2u);
}
TEST(ChatQueue, OversizedSubtitleCannotCancelSubmittedReply) {
    ChatQueue q; q.Start("Translate"); q.Add("First.");
    auto first=q.Poll("a",false,false,0); ASSERT_TRUE(first); ASSERT_TRUE(q.Sent(first->id));
    EXPECT_FALSE(q.Add(std::string(12001,'x')));
    EXPECT_TRUE(q.View().enabled); EXPECT_FALSE(q.View().detail.empty());
    EXPECT_TRUE(q.Complete(first->id,"Reply"));
}
TEST(ChatQueue, LostCompletionResponseCanBeAcknowledgedAgain) {
    ChatQueue q; q.Start("Translate"); q.Add("First."); q.Add("Second.");
    auto first=q.Poll("a",false,false,0); ASSERT_TRUE(first); ASSERT_TRUE(q.Sent(first->id));
    ASSERT_TRUE(q.Complete(first->id,"Reply"));
    auto second=q.Poll("a",false,false,1); ASSERT_TRUE(second);
    EXPECT_TRUE(q.Complete(first->id,"Reply"));
    EXPECT_EQ(q.View().completed,1u);
    EXPECT_FALSE(q.Complete(first->id,"Different reply"));
    EXPECT_TRUE(q.Sent(second->id)); EXPECT_TRUE(q.Complete(second->id,"Reply"));
    EXPECT_EQ(q.View().completed,2u);
}
TEST(ChatQueue, UiStatusSeparatesLeaseSubmissionFailureAndExportsQueue) {
    ChatQueue q;q.Start("Translate");q.Add("First.");q.Add("Second.");
    auto task=q.Poll("a",false,false,0);ASSERT_TRUE(task);
    EXPECT_EQ(q.View().in_flight,1u);EXPECT_EQ(q.View().submitted,0u);EXPECT_EQ(q.View().completed,0u);
    q.Sent(task->id);EXPECT_EQ(q.View().submitted,1u);
    EXPECT_NE(q.PendingText().find("Second."),std::string::npos);
    q.Fail(task->id,"uncertain");EXPECT_EQ(q.View().failed,1u);EXPECT_EQ(q.View().completed,0u);
}
TEST(ChatQueue, ActiveTaskHeartbeatExtendsLeaseWithoutRevivingStoppedTask) {
    ChatQueue q; q.Start("Translate"); q.Add("First.");
    auto first=q.Poll("a",false,false,0); ASSERT_TRUE(first); ASSERT_TRUE(q.Sent(first->id));
    EXPECT_FALSE(q.Progress(first->id+1,170000,"wrong task"));
    EXPECT_TRUE(q.Progress(first->id,170000,"Waiting"));
    q.CheckTimeout(180001); EXPECT_TRUE(q.View().enabled);
    EXPECT_FALSE(q.Progress(first->id,180002,std::string(513,'x')));
    q.Stop(); EXPECT_FALSE(q.Progress(first->id,180003,"Waiting"));
}
