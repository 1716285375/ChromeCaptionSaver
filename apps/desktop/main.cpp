#include "ChromeCaptionSaver/platform/RelayBridge.h"
// Native Win32/DX11 shell adapted from Dear ImGui's MIT-licensed example.
#include "imgui.h"
#include "imgui_internal.h"
#include "TranscriptView.h"
#include "ChromeCaptionSaver/core/Paths.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "ChromeCaptionSaver/capture/RecordingSession.h"
#include "ChromeCaptionSaver/platform/WindowLocator.h"
#include "ChromeCaptionSaver/platform/UiaClient.h"
#include "ChromeCaptionSaver/export/SubtitleExporter.h"
#include <d3d11.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <future>
#include <filesystem>
#include <array>
#include <cstdio>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include "ui/RenderSnapshot.h"

namespace fs = std::filesystem;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static bool g_close = false, g_minimized = false;
bool CreateDeviceD3D(HWND hwnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

#include "ui/Theme.h"
#include "ui/Typography.h"
#include "DesktopUi.h"

LRESULT WINAPI WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam)) return true;
    switch (message) {
        case WM_SIZE:
            g_minimized = wparam == SIZE_MINIMIZED;
            if (!g_minimized) { g_ResizeWidth = LOWORD(lparam); g_ResizeHeight = HIWORD(lparam); }
            return 0;
        case WM_GETMINMAXINFO: {
            const float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
            auto info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize = {static_cast<LONG>(1040 * scale), static_cast<LONG>(640 * scale)};
            return 0;
        }
        case WM_DPICHANGED: {
            auto rect = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SYSCOMMAND: if ((wparam & 0xfff0) == SC_KEYMENU) return 0; break;
        case WM_CLOSE: g_close = true; return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int) {
    const auto arguments=std::wstring(command_line);
    const bool smoke_test=arguments.starts_with(L"--smoke-test");
    const bool ui_preview=arguments.starts_with(L"--ui-preview");
    const bool render_preview=arguments.starts_with(L"--render-preview");
    std::wstring snapshot_path;
    int test_width=1600,test_height=900,preview_page=0;float test_scale=0;
    if(smoke_test)swscanf_s(command_line,L"--smoke-test %d %d %f",&test_width,&test_height,&test_scale);
    if(ui_preview)swscanf_s(command_line,L"--ui-preview %d",&preview_page);
    if(render_preview){std::wstring flag;std::wistringstream input(arguments);input>>flag>>preview_page>>test_width>>test_height>>test_scale>>std::quoted(snapshot_path);if(snapshot_path.empty()||test_width<1040||test_height<640||test_scale<1||test_scale>1.5)return 3;}
    bool rendered = false;
    int layout_errors = 0;
    spdlog::set_level(spdlog::level::warn);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    const float initial_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"ChromeCaptionSaverDesktop", nullptr};
    RegisterClassExW(&wc);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const int width = std::min(static_cast<int>(test_width * initial_scale), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    const int height = std::min(static_cast<int>(test_height * initial_scale), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
    auto hwnd = CreateWindowW(wc.lpszClassName, L"ChromeCaptionSaver · 字幕工作室", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, instance, nullptr);
    if (!hwnd || !CreateDeviceD3D(hwnd)) {
        MessageBoxW(nullptr, L"无法创建 DirectX 11 窗口。", L"ChromeCaptionSaver", MB_ICONERROR);
        CleanupDeviceD3D(); if (hwnd) DestroyWindow(hwnd); if (SUCCEEDED(com)) CoUninitialize(); return 1;
    }
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    if (smoke_test||render_preview) {
        io.ConfigErrorRecoveryEnableAssert = false;
        ImGui::GetCurrentContext()->ErrorCallbackUserData = &layout_errors;
        ImGui::GetCurrentContext()->ErrorCallback = [](ImGuiContext*, void* count, const char*) { ++*static_cast<int*>(count); };
    }
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ccs::ui::ApplyTheme();
    auto& style=ImGui::GetStyle();
    const auto base_style=style;
    ccs::ui::LoadTypography();
    if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext)) {
        MessageBoxW(hwnd, L"界面渲染初始化失败。", L"ChromeCaptionSaver", MB_ICONERROR); return 1;
    }
    if (!smoke_test&&!render_preview) { ShowWindow(hwnd, SW_SHOWDEFAULT); UpdateWindow(hwnd); }
    {
        Desktop desktop(!smoke_test&&!ui_preview&&!render_preview);
        if(ui_preview||render_preview)desktop.page=std::clamp(preview_page,0,3);
        int smoke_frames = 0;
        float previous_scale = 0;
        bool done = false;
        while (!done) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); if (message.message == WM_QUIT) done = true; }
            if (done) break;
            desktop.Poll();
            if (g_close) { desktop.relay.Stop(); if (!desktop.session.View().running) break; desktop.session.Stop(); desktop.stopping = true; }
            if (g_minimized) { Sleep(50); continue; }
            if(render_preview&&smoke_frames==0){g_ResizeWidth=static_cast<UINT>(test_width*test_scale);g_ResizeHeight=static_cast<UINT>(test_height*test_scale);}
            if (g_ResizeWidth && g_ResizeHeight) {
                CleanupRenderTarget();
                const HRESULT resized = g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
                g_ResizeWidth = g_ResizeHeight = 0;
                if (FAILED(resized)) break;
                CreateRenderTarget();
                if (!g_mainRenderTargetView) break;
            }
            const float scale = (smoke_test||render_preview)&&test_scale>0?test_scale:ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
            if (scale != previous_scale) { style = base_style; style.ScaleAllSizes(scale); style.FontScaleDpi = scale; previous_scale = scale; }
            ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame();
            if(smoke_test||render_preview)io.DisplaySize={test_width*scale,test_height*scale};
            ImGui::NewFrame();
            desktop.Draw(hwnd, scale, smoke_test||ui_preview||render_preview,ui_preview||render_preview);
            ImGui::Render();
            rendered = ImGui::GetDrawData()->TotalVtxCount > 0;
            const float clear[] = {.055f, .075f, .105f, 1};
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if(render_preview&&++smoke_frames>=8){if(!SaveRenderSnapshot(g_pSwapChain,g_pd3dDevice,g_pd3dDeviceContext,snapshot_path.c_str()))++layout_errors;break;}
            if (smoke_test && ++smoke_frames >= 8) break;
            const HRESULT presented = g_pSwapChain->Present(1, 0);
            if (presented == DXGI_STATUS_OCCLUDED) Sleep(50);
            else if (FAILED(presented)) { desktop.session.Stop(); MessageBoxW(hwnd, L"图形设备已断开，正在保存录制。", L"ChromeCaptionSaver", MB_ICONERROR); break; }
        }
    }
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    CleanupDeviceD3D(); DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName, instance);
    if (SUCCEEDED(com)) CoUninitialize();
    return (smoke_test||render_preview) && (layout_errors > 0 || !rendered) ? 2 : 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    // This is a basic setup. Optimally could use e.g. DXGI_SWAP_EFFECT_FLIP_DISCARD and handle fullscreen mode differently. See #8979 for suggestions.
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return g_mainRenderTargetView != nullptr;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}
