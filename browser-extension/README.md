# 首次连接

1. Chrome 打开 chrome://extensions，启用开发者模式，加载本文件所在的 ChatGPTBridge 文件夹。
2. 在已登录的 https://chatgpt.com/ 标签页刷新。EXE 中打开“ChatGPT 联动”，复制连接码。
3. 在该 ChatGPT 标签页打开扩展，粘贴连接码，点击绑定。EXE 中点击“启动联动”，开始录制。

EXE 可随时停止后续发送。每次 EXE 重启后重新配对。请保留专用会话标签页，不要在联动过程中切换会话或手动发送。

扩展不处理登录、验证码或额度限制。网页改版导致控件识别失败时会暂停，不自动重发。对话日志在项目 data/sessions 下；无需 API Key。

测试：项目 browser-extension 目录执行 `node --test test.cjs`。测试使用模拟网页驱动，真实账号网页需要另外验证。
