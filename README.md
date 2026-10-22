# ChromeCaptionSaver

Windows 11 x64 · C++20 · Dear ImGui · Win32 / DirectX 11

通过 Windows UI Automation 读取 Chrome 实时字幕浮窗，保存原始快照并导出文本与字幕文件。不录音，不做 OCR，不调用语音识别服务。

## 下载

从 [Releases](https://github.com/1716285375/ChromeCaptionSaver/releases) 下载 Windows x64 压缩包，解压后运行 `ChromeCaptionSaver.exe`。保留同目录的 `ChatGPTBridge` 文件夹。

发布包的数据保存在 `%LOCALAPPDATA%/ChromeCaptionSaver/data`；源码构建默认使用项目 `data/`。

## 桌面使用

1. 在 Chrome 中开启实时字幕并播放音频。
2. 在“设置”页检测字幕窗口，选择字幕来源与保存目录。
3. 在“录制与阅读”页开始录制，可暂停、继续、停止并保存。
4. 使用“导出 TXT / SRT / VTT”导出；在“历史记录”页查看或重新导出已有会话。

字幕与回答支持拖动选择、Ctrl+A 全选、Ctrl+C 复制。选择、滚轮阅读或搜索会暂停跟随，按 Esc 恢复。历史列表与预览之间的分隔线可拖动调整宽度。

默认保存至编译时项目根目录的 `data/sessions`，GUI 与 CLI 共用此默认值，也可选择其他目录。界面设置保存在 `data/ui-settings.json`。

每次录制创建独立会话目录，原始快照为 `snapshots.jsonl`，导出位于 `exports/<独立编号>`：

- `transcript.txt`：合并文稿。
- `segments.jsonl`：分段、质量标记与快照引用。
- `captions.approx.srt`、`captions.approx.vtt`：近似时间轴字幕。

已有导出不会覆盖；最小化不停止录制，关闭窗口会结束采集并尝试导出。

## ChatGPT 网页联动（可选）

1. 构建桌面程序后，在 `chrome://extensions` 开启开发者模式，加载 `build/Release/ChatGPTBridge`（Debug 构建使用 `build/Debug/ChatGPTBridge`）。
2. 打开并登录 [ChatGPT](https://chatgpt.com/)，刷新页面。在桌面“ChatGPT 联动”页复制连接码，在扩展中粘贴并绑定当前标签页。
3. 编辑提示词，启动联动，发送测试字幕验证提交与回复回传，再开始录制。

稳定字幕逐句入队；上一条回复确认完成后才发送下一条。使用专用会话标签页。已有草稿不会覆盖；异常或超时暂停，未确认任务不自动重发。停止联动会清空待发送队列，原始录制保留；可先导出未完成项。

通过随机连接码鉴权的本机回环服务通信，不读取账号 Cookie，不需要 API Key。启用联动会将字幕发送到绑定网页，字幕与回复日志保存在会话目录。重启桌面程序后需要重新配对；更新扩展后重新加载扩展、刷新网页并绑定。

登录、验证码和额度限制需在 Chrome 中处理。网页控件可能变化；模拟测试不能代替真实账号持续运行验证。

## 构建

需要 Visual Studio 2022 的 C++ 桌面开发组件、Windows SDK、CMake ≥ 3.26 和 vcpkg。Dear ImGui 固定为 Git 子模块，必须一并获取。

```powershell
git submodule update --init --recursive
$env:VCPKG_ROOT = 'C:\src\vcpkg' # 改为实际 vcpkg 安装路径
cmake --preset debug
cmake --build build/debug --config Release
ctest --test-dir build/debug -C Release --output-on-failure
.\build\Release\ChromeCaptionSaver.exe
```

Debug 构建可使用 `cmake --build --preset debug`，程序位于 `build/Debug`。仅需命令行工具时设置 `-DCCS_BUILD_GUI=OFF`。浏览器协议测试需要 Node.js。

## 命令行

```powershell
caption_probe --list-windows
caption_probe --probe --duration 10
caption_probe --record --duration 60 --outdir .\data\new-session
caption_probe --record --hwnd 0x123456 --duration 60
caption_probe --export .\data\new-session\snapshots.jsonl --outdir .\data\new-export
caption_probe --dump-tree --hwnd 0x123456 --depth 12 --output .\data\diagnosis.json
```

输出使用 UTF-8；导出目标须是尚不存在的新目录。损坏 JSONL、非法时间戳和序号返回错误。Ctrl+C / Ctrl+Break 会结束录制并尝试导出。

## 自动构建与发布

推送到 `main` 或提交 Pull Request 会自动编译、测试并生成 Windows x64 下载包；也可在 Actions 手动运行。版本发布使用标签：

```powershell
git tag v0.3.7
git push origin v0.3.7
```

标签构建通过后，自动创建 GitHub Release 并附上压缩包。发布包静态链接运行库，包含桌面程序、命令行工具、浏览器扩展和第三方许可。
