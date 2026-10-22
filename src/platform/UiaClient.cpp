#include "ChromeCaptionSaver/platform/UiaClient.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <functional>

namespace ccs::platform {

UiaClient::UiaClient() = default;

UiaClient::~UiaClient() {
    Shutdown();
}

bool UiaClient::Initialize() {
    if (uia_) return true;
    Shutdown();
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        last_hr_ = hr;
        spdlog::error("CoInitializeEx failed: {:#x}", static_cast<unsigned>(hr));
        return false;
    }
    com_inited_ = SUCCEEDED(hr);

    hr = ::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&uia_));
    if (FAILED(hr)) {
        last_hr_ = hr;
        spdlog::error("CoCreateInstance(CUIAutomation) failed: {:#x}", static_cast<unsigned>(hr));
        Shutdown();
        return false;
    }

    hr = uia_->GetRootElement(&root_el_);
    if (FAILED(hr)) {
        last_hr_ = hr;
        spdlog::error("get_RootElement failed: {:#x}", static_cast<unsigned>(hr));
        Shutdown();
        return false;
    }
    last_hr_ = S_OK;
    spdlog::debug("UiaClient initialized (MTA)");
    return true;
}

void UiaClient::Shutdown() {
    root_el_.Reset();
    uia_.Reset();
    if (com_inited_) {
        ::CoUninitialize();
        com_inited_ = false;
    }
}

Microsoft::WRL::ComPtr<IUIAutomationElement> UiaClient::ElementFromHandle(HWND hwnd) {
    Microsoft::WRL::ComPtr<IUIAutomationElement> el;
    if (!uia_) return el;
    HRESULT hr = uia_->ElementFromHandle(hwnd, &el);
    last_hr_ = hr;
    if (FAILED(hr)) {
        spdlog::warn("ElementFromHandle failed: {:#x}", static_cast<unsigned>(hr));
    }
    return el;
}

std::vector<UiaNodeInfo> UiaClient::DumpTree(IUIAutomationElement* root, int max_depth) {
    std::vector<UiaNodeInfo> out;
    if (!uia_ || !root) return out;

    Microsoft::WRL::ComPtr<IUIAutomationTreeWalker> walker;
    HRESULT hr = uia_->get_RawViewWalker(&walker);
    if (FAILED(hr)) { last_hr_ = hr; return out; }

    std::function<void(IUIAutomationElement*, int)> walk = [&](IUIAutomationElement* el, int depth) {
        if (!el || depth > max_depth) return;

        UiaNodeInfo info;
        info.depth = depth;

        CONTROLTYPEID ct = 0;
        el->get_CurrentControlType(&ct);
        info.control_type = ct;

        BSTR name = nullptr;
        if (SUCCEEDED(el->get_CurrentName(&name)) && name) {
            info.name = name;
            ::SysFreeString(name);
        }
        BSTR aid = nullptr;
        if (SUCCEEDED(el->get_CurrentAutomationId(&aid)) && aid) {
            info.automation_id = aid;
            ::SysFreeString(aid);
        }
        BSTR cn = nullptr;
        if (SUCCEEDED(el->get_CurrentClassName(&cn)) && cn) {
            info.class_name = cn;
            ::SysFreeString(cn);
        }
        BOOL off = FALSE;
        el->get_CurrentIsOffscreen(&off);
        info.is_offscreen = off != FALSE;

        Microsoft::WRL::ComPtr<IUnknown> pat;
        if (SUCCEEDED(el->GetCurrentPattern(UIA_TextPatternId, &pat)) && pat) {
            info.support_text_pattern = L"yes";
        } else {
            info.support_text_pattern = L"no";
        }

        out.push_back(std::move(info));
        if (depth == max_depth) return;

        Microsoft::WRL::ComPtr<IUIAutomationElement> child;
        walker->GetFirstChildElement(el, &child);
        while (child) {
            walk(child.Get(), depth + 1);
            Microsoft::WRL::ComPtr<IUIAutomationElement> next;
            walker->GetNextSiblingElement(child.Get(), &next);
            child = next;
        }
    };

    walk(root, 0);
    return out;
}

bool UiaClient::ReadCaptionText(IUIAutomationElement* window_el,
                                std::vector<std::wstring>& out_lines,
                                std::wstring& out_method) {
    out_lines.clear();
    out_method.clear();
    if (!uia_ || !window_el) return false;

    // Scope extraction to Chrome's caption label; never collect a browser page's text.
    VARIANT class_name;
    ::VariantInit(&class_name);
    class_name.vt = VT_BSTR;
    class_name.bstrVal = ::SysAllocString(L"CaptionBubbleLabel");
    if (!class_name.bstrVal) { last_hr_ = E_OUTOFMEMORY; return false; }
    Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
    HRESULT hr = uia_->CreatePropertyCondition(UIA_ClassNamePropertyId, class_name, &condition);
    ::VariantClear(&class_name);
    if (FAILED(hr)) { last_hr_ = hr; return false; }

    Microsoft::WRL::ComPtr<IUIAutomationElement> caption;
    hr = window_el->FindFirst(TreeScope_Subtree, condition.Get(), &caption);
    if (FAILED(hr) || !caption) {
        last_hr_ = FAILED(hr) ? hr : UIA_E_ELEMENTNOTAVAILABLE;
        return false;
    }
    BSTR name = nullptr;
    hr = caption->get_CurrentName(&name);
    std::wstring text = name ? name : L"";
    if (name) ::SysFreeString(name);
    if (FAILED(hr)) { last_hr_ = hr; return false; }
    out_method = L"Document.Name";
    if (!text.empty()) {
        size_t start = 0;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == L'\n' || text[i] == L'\r') {
                if (i > start) out_lines.emplace_back(text.substr(start, i - start));
                if (i < text.size() && text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
                start = i + 1;
            }
        }
    } else {
        // Older layouts may expose text as virtual children of the caption label.
        VARIANT type;
        ::VariantInit(&type);
        type.vt = VT_I4;
        type.lVal = UIA_TextControlTypeId;
        hr = uia_->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &condition);
        if (FAILED(hr)) { last_hr_ = hr; return false; }
        Microsoft::WRL::ComPtr<IUIAutomationElementArray> children;
        hr = caption->FindAll(TreeScope_Descendants, condition.Get(), &children);
        if (FAILED(hr)) { last_hr_ = hr; return false; }
        int count = 0;
        if (children) children->get_Length(&count);
        for (int i = 0; i < count; ++i) {
            Microsoft::WRL::ComPtr<IUIAutomationElement> child;
            if (FAILED(children->GetElement(i, &child)) || !child) continue;
            BSTR value = nullptr;
            hr = child->get_CurrentName(&value);
            if (SUCCEEDED(hr) && value && *value) out_lines.emplace_back(value);
            if (value) ::SysFreeString(value);
            if (FAILED(hr)) { last_hr_ = hr; return false; }
        }
        out_method = L"Text.Name";
    }
    last_hr_ = S_OK;
    return true; // An empty, accessible caption label is a valid silent interval.

}

Microsoft::WRL::ComPtr<IUIAutomationElement> UiaClient::ElementFromPoint(POINT p) {
    Microsoft::WRL::ComPtr<IUIAutomationElement> el;
    if (!uia_) return el;
    HRESULT hr = uia_->ElementFromPoint(p, &el);
    last_hr_ = hr;
    return el;
}

const wchar_t* ControlTypeName(CONTROLTYPEID ct) {
    switch (ct) {
        case UIA_ButtonControlTypeId:     return L"Button";
        case UIA_CalendarControlTypeId:    return L"Calendar";
        case UIA_CheckBoxControlTypeId:   return L"CheckBox";
        case UIA_ComboBoxControlTypeId:   return L"ComboBox";
        case UIA_EditControlTypeId:       return L"Edit";
        case UIA_HyperlinkControlTypeId:  return L"Hyperlink";
        case UIA_ImageControlTypeId:      return L"Image";
        case UIA_ListItemControlTypeId:   return L"ListItem";
        case UIA_ListControlTypeId:       return L"List";
        case UIA_MenuControlTypeId:       return L"Menu";
        case UIA_MenuBarControlTypeId:     return L"MenuBar";
        case UIA_MenuItemControlTypeId:   return L"MenuItem";
        case UIA_ProgressBarControlTypeId: return L"ProgressBar";
        case UIA_RadioButtonControlTypeId: return L"RadioButton";
        case UIA_ScrollBarControlTypeId:  return L"ScrollBar";
        case UIA_SliderControlTypeId:     return L"Slider";
        case UIA_SpinnerControlTypeId:    return L"Spinner";
        case UIA_StatusBarControlTypeId:  return L"StatusBar";
        case UIA_TabControlTypeId:        return L"Tab";
        case UIA_TabItemControlTypeId:    return L"TabItem";
        case UIA_TextControlTypeId:       return L"Text";
        case UIA_ToolBarControlTypeId:    return L"ToolBar";
        case UIA_ToolTipControlTypeId:    return L"ToolTip";
        case UIA_TreeControlTypeId:       return L"Tree";
        case UIA_TreeItemControlTypeId:   return L"TreeItem";
        case UIA_CustomControlTypeId:     return L"Custom";
        case UIA_GroupControlTypeId:      return L"Group";
        case UIA_ThumbControlTypeId:      return L"Thumb";
        case UIA_DataGridControlTypeId:   return L"DataGrid";
        case UIA_DataItemControlTypeId:   return L"DataItem";
        case UIA_DocumentControlTypeId:   return L"Document";
        case UIA_SplitButtonControlTypeId: return L"SplitButton";
        case UIA_WindowControlTypeId:     return L"Window";
        case UIA_PaneControlTypeId:       return L"Pane";
        case UIA_HeaderControlTypeId:     return L"Header";
        case UIA_HeaderItemControlTypeId: return L"HeaderItem";
        case UIA_TableControlTypeId:      return L"Table";
        default:                          return L"?";
    }
}

} // namespace ccs::platform
