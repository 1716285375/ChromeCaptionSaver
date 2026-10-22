#pragma once
#include "ChromeCaptionSaver/core/Paths.h"
#include <filesystem>
#include <shellapi.h>
#include <shobjidl.h>
#include <string>
#include <wrl/client.h>
namespace fs = std::filesystem;

inline std::string Utf8(const std::wstring &text) {
  if (text.empty())
    return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                    static_cast<int>(text.size()), nullptr, 0,
                                    nullptr, nullptr);
  std::string result(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      result.data(), n, nullptr, nullptr);
  return result;
}
inline fs::path Path(const char *utf8) {
  std::string text(utf8);
  return fs::path(std::u8string(text.begin(), text.end()));
}
inline std::string PathText(const fs::path &path) {
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}
inline fs::path PickPath(HWND owner, bool folder) {
  Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return {};
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                     (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
  dialog->SetTitle(folder ? L"选择录制保存目录" : L"选择 snapshots.jsonl");
  COMDLG_FILTERSPEC filter[] = {{L"字幕快照 JSONL", L"*.jsonl"}};
  if (!folder)
    dialog->SetFileTypes(1, filter);
  if (FAILED(dialog->Show(owner)))
    return {};
  Microsoft::WRL::ComPtr<IShellItem> item;
  if (FAILED(dialog->GetResult(&item)))
    return {};
  PWSTR path = nullptr;
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
    return {};
  fs::path result(path);
  CoTaskMemFree(path);
  return result;
}
inline const char *StateName(ccs::core::CaptureState state) {
  using ccs::core::CaptureState;
  switch (state) {
  case CaptureState::Idle:
    return "等待录制";
  case CaptureState::Discovering:
    return "正在连接字幕窗口";
  case CaptureState::Capturing:
    return "正在录制";
  case CaptureState::Paused:
    return "录制已暂停";
  case CaptureState::Finalizing:
    return "正在保存与导出";
  case CaptureState::TargetNotFound:
    return "字幕连接已断开";
  case CaptureState::Stopped:
    return "录制已结束";
  default:
    return "正在准备";
  }
}
