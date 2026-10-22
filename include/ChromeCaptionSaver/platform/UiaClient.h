#pragma once
// Thin RAII wrapper around IUIAutomation for reading Chrome Live Caption.
#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdint>

namespace ccs::platform {

struct UiaNodeInfo {
    CONTROLTYPEID control_type = 0;
    std::wstring name;
    std::wstring automation_id;
    std::wstring class_name;
    bool       is_offscreen = false;
    int        depth = 0;
    std::wstring support_text_pattern;
};

class UiaClient {
public:
    UiaClient();
    ~UiaClient();

    UiaClient(const UiaClient&) = delete;
    UiaClient& operator=(const UiaClient&) = delete;

    bool Initialize();
    void Shutdown();

    Microsoft::WRL::ComPtr<IUIAutomationElement> ElementFromHandle(HWND hwnd);
    std::vector<UiaNodeInfo> DumpTree(IUIAutomationElement* root, int max_depth);
    // False means missing/inaccessible caption control; true may have zero text lines.
    bool ReadCaptionText(IUIAutomationElement* window_el,
                         std::vector<std::wstring>& out_lines,
                         std::wstring& out_method);
    Microsoft::WRL::ComPtr<IUIAutomationElement> ElementFromPoint(POINT p);

    HRESULT LastHr() const { return last_hr_; }

private:
    Microsoft::WRL::ComPtr<IUIAutomation>      uia_;
    Microsoft::WRL::ComPtr<IUIAutomationElement> root_el_;
    HRESULT last_hr_ = S_OK;
    bool    com_inited_ = false;
};

const wchar_t* ControlTypeName(CONTROLTYPEID ct);

} // namespace ccs::platform
