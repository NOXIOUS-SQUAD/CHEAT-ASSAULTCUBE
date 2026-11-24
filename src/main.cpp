#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include "memory.h"
#include "ui.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain1* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTarget = nullptr;
IDCompositionDevice* g_composition = nullptr;
IDCompositionTarget* g_compositionTarget = nullptr;
IDCompositionVisual* g_compositionVisual = nullptr;
HWND g_window = nullptr;

UINT g_width = 0;
UINT g_height = 0;
bool g_running = true;
bool g_prevInsert = false;
bool g_prevEnd = false;

MemoryWriter g_memory;
UiState g_state;

void ReleaseRenderTarget()
{
    if (g_renderTarget != nullptr)
    {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }
}

bool CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer))))
        return false;

    const HRESULT hr = g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
    backBuffer->Release();
    return SUCCEEDED(hr);
}

bool CreateDevice()
{
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                   levels, 3, D3D11_SDK_VERSION, &g_device, &obtained, &g_context);
    if (FAILED(hr))
        return false;

    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(g_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))))
        return false;

    IDXGIAdapter* adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);

    IDXGIFactory2* factory = nullptr;
    if (adapter != nullptr)
        adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));

    if (factory == nullptr)
    {
        if (adapter != nullptr) adapter->Release();
        dxgiDevice->Release();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = g_width;
    desc.Height = g_height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = factory->CreateSwapChainForComposition(g_device, &desc, nullptr, &g_swapChain);

    factory->Release();
    adapter->Release();
    dxgiDevice->Release();

    if (FAILED(hr))
        return false;

    hr = DCompositionCreateDevice(nullptr, __uuidof(IDCompositionDevice), reinterpret_cast<void**>(&g_composition));
    if (FAILED(hr))
        return false;

    g_composition->CreateTargetForHwnd(g_window, TRUE, &g_compositionTarget);
    g_composition->CreateVisual(&g_compositionVisual);
    g_compositionVisual->SetContent(g_swapChain);
    g_compositionTarget->SetRoot(g_compositionVisual);
    g_composition->Commit();

    return CreateRenderTarget();
}

void DestroyDevice()
{
    ReleaseRenderTarget();

    if (g_compositionVisual != nullptr)
    {
        g_compositionVisual->Release();
        g_compositionVisual = nullptr;
    }
    if (g_compositionTarget != nullptr)
    {
        g_compositionTarget->Release();
        g_compositionTarget = nullptr;
    }
    if (g_composition != nullptr)
    {
        g_composition->Release();
        g_composition = nullptr;
    }
    if (g_swapChain != nullptr)
    {
        g_swapChain->Release();
        g_swapChain = nullptr;
    }
    if (g_context != nullptr)
    {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device != nullptr)
    {
        g_device->Release();
        g_device = nullptr;
    }
}

void UpdateHotkeys()
{
    const bool insertDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (insertDown && !g_prevInsert)
        g_state.open = !g_state.open;
    g_prevInsert = insertDown;

    const bool endDown = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
    if (endDown && !g_prevEnd)
        g_running = false;
    g_prevEnd = endDown;
}

void ResizeOverlay()
{
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_width = static_cast<UINT>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    g_height = static_cast<UINT>(GetSystemMetrics(SM_CYVIRTUALSCREEN));

    SetWindowPos(g_window, HWND_TOPMOST, vx, vy, static_cast<int>(g_width), static_cast<int>(g_height),
                 SWP_NOACTIVATE);

    if (g_swapChain != nullptr)
    {
        ReleaseRenderTarget();
        g_swapChain->ResizeBuffers(0, g_width, g_height, DXGI_FORMAT_UNKNOWN, 0);
        CreateRenderTarget();
        if (g_composition != nullptr)
            g_composition->Commit();
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
        return true;

    switch (msg)
    {
    case WM_NCHITTEST:
    {
        if (!g_state.open)
            return HTTRANSPARENT;

        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ScreenToClient(hwnd, &pt);

        const ImVec2 pos = ui::MenuPosition();
        const ImVec2 size = ui::MenuSize();
        if (pt.x >= pos.x && pt.x <= pos.x + size.x && pt.y >= pos.y && pt.y <= pos.y + size.y)
            return HTCLIENT;

        return HTTRANSPARENT;
    }
    case WM_DISPLAYCHANGE:
        ResizeOverlay();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"AssaultControlOverlay";

    if (!RegisterClassExW(&wc))
        return 1;

    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_width = static_cast<UINT>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    g_height = static_cast<UINT>(GetSystemMetrics(SM_CYVIRTUALSCREEN));

    g_window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
                               wc.lpszClassName, L"Assault Control", WS_POPUP, vx, vy,
                               static_cast<int>(g_width), static_cast<int>(g_height),
                               nullptr, nullptr, instance, nullptr);
    if (g_window == nullptr)
        return 1;

    if (!CreateDevice())
    {
        DestroyDevice();
        DestroyWindow(g_window);
        UnregisterClassW(wc.lpszClassName, instance);
        return 1;
    }

    ShowWindow(g_window, SW_SHOWNOACTIVATE);
    UpdateWindow(g_window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    float scale = static_cast<float>(ImGui_ImplWin32_GetDpiScaleForHwnd(g_window));
    if (scale <= 0.0f)
        scale = 1.0f;

    ui::LoadFonts(scale);
    ui::ApplyStyle();

    ImGui_ImplWin32_Init(g_window);
    ImGui_ImplDX11_Init(g_device, g_context);

    std::vector<TargetSpec> targets;
    targets.push_back(TargetSpec{ 0x00907840, false });
    targets.push_back(TargetSpec{ 0x1886C89C, true });

    g_memory.Configure(L"ac_client.exe", targets);
    g_memory.SetValue(20);
    g_memory.SetLock(true);
    g_memory.Start();

    while (g_running)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                g_running = false;
        }

        if (!g_running)
            break;

        UpdateHotkeys();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ui::Draw(g_state, g_memory);

        ImGui::Render();

        const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        g_context->ClearRenderTargetView(g_renderTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_swapChain->Present(1, 0);
    }

    g_memory.Stop();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    DestroyDevice();
    DestroyWindow(g_window);
    UnregisterClassW(wc.lpszClassName, instance);

    return 0;
}
