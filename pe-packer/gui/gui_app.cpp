#define WIN32_LEAN_AND_MEAN
#include "gui_app.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <commdlg.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <sstream>
#include <utility>
#include <vector>
#include <iomanip>
#include <cstring>
#include <fstream>
#include <cwctype>
#include <cfloat>

#include "../core/core.hpp"
#include "../core/mutation_profile.hpp"
#include "../handler/handler.hpp"
#include "../utils/arguments.hpp"
#include "../utils/utils.hpp"
#include "../pe_raw/pe_view.hpp"

#include "../../vendor/imgui/imgui.h"
#include "../../vendor/imgui/backends/imgui_impl_dx11.h"
#include "../../vendor/imgui/backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LogCallback g_logCallback = nullptr;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

static constexpr int kGuiWidth = 800;
static constexpr int kGuiHeight = 600;
static constexpr float kUIFontSize = 17.0f;
static constexpr float kLogsHeight = 196.0f;

// interactive elements only
static const ImVec4 kClrAccent(0.52f, 0.86f, 0.68f, 1.0f);
static const ImVec4 kClrAccentHover(0.58f, 0.92f, 0.74f, 1.0f);
static const ImVec4 kClrAccentActive(0.36f, 0.66f, 0.52f, 1.0f);
static const ImVec4 kClrAccentSoft(0.52f, 0.86f, 0.68f, 0.22f);
static const ImVec4 kClrAccentGlow(0.52f, 0.86f, 0.68f, 0.12f);

// backgrounds & surfaces
static const ImVec4 kClrBg(0.09f, 0.10f, 0.11f, 1.0f);
static const ImVec4 kClrPanel(0.13f, 0.14f, 0.16f, 1.0f);
static const ImVec4 kClrSurface(0.17f, 0.18f, 0.21f, 1.0f);
static const ImVec4 kClrSurfaceHi(0.21f, 0.23f, 0.26f, 1.0f);

// text
static const ImVec4 kClrText(0.90f, 0.91f, 0.93f, 1.0f);
static const ImVec4 kClrTextTitle(0.94f, 0.96f, 0.95f, 1.0f);
static const ImVec4 kClrTextDim(0.50f, 0.53f, 0.57f, 1.0f);

// borders & dividers
static const ImVec4 kClrBorder(0.30f, 0.32f, 0.36f, 0.42f);
static const ImVec4 kClrBorderSubtle(0.22f, 0.24f, 0.27f, 0.55f);
static const ImVec4 kClrSeparator(0.22f, 0.24f, 0.27f, 1.0f);

// semantic
static const ImVec4 kClrLogOk(0.52f, 0.86f, 0.68f, 1.0f);
static const ImVec4 kClrLogErr(0.92f, 0.50f, 0.44f, 1.0f);

// buttons
static const ImVec4 kClrBtnNeutralLo(0.16f, 0.17f, 0.19f, 1.0f);
static const ImVec4 kClrBtnNeutralHi(0.22f, 0.24f, 0.27f, 1.0f);
static const ImVec4 kClrPackLo(0.24f, 0.42f, 0.34f, 1.0f);
static const ImVec4 kClrPackHi(0.34f, 0.58f, 0.46f, 1.0f);
static const ImVec4 kClrPackHoverLo(0.20f, 0.36f, 0.28f, 1.0f);
static const ImVec4 kClrPackHoverHi(0.28f, 0.50f, 0.38f, 1.0f);

// controls
static const ImVec4 kClrCheckMark(0.96f, 0.97f, 0.98f, 1.0f);
static const ImVec4 kClrSliderKnob(0.92f, 0.93f, 0.95f, 1.0f);
static const ImVec4 kClrSliderKnobActive(0.98f, 0.98f, 1.0f, 1.0f);
static const ImVec4 kClrScrollbarGrab(0.32f, 0.34f, 0.38f, 0.50f);
static const ImVec4 kClrScrollbarGrabHovered(0.40f, 0.42f, 0.46f, 0.70f);
static constexpr float kPanelPad = 10.0f;
static constexpr float kPanelTitleGap = 4.0f;
static constexpr float kBrowseBtnWidth = 96.0f;
static constexpr float kPackBtnWidth = 112.0f;
static constexpr float kBrowseGap = 8.0f;
static constexpr float kFormLabelWidth = 78.0f;
static constexpr float kFieldLabelWidth = 92.0f;
static constexpr float kInfoLabelWidth = 88.0f;
static constexpr float kOptionsColumnGap = 12.0f;
static constexpr ImGuiWindowFlags kPanelChildFlags =
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

struct FileInfo {
    std::string architecture;
    std::string entryPoint;
    std::string imageBase;
    std::string imageSize;
    std::string sectionCount;
    std::string filePath;
    bool aslrEnabled = false;
    bool isValid = false;
};

struct GuiState {
    std::array<char, MAX_PATH> input{};
    std::array<char, MAX_PATH> output{};
    // display buffers: show only filename until the user focuses the field
    std::array<char, MAX_PATH> inputDisplay{};
    std::array<char, MAX_PATH> outputDisplay{};
    std::array<char, 48> fpackStart{};
    std::array<char, 48> fpackEnd{};
    int mutationBase = 1;
    bool removeAslr = false;
    bool obfuscateOep = false;
    bool antiDisasm = false;
    bool mba = false;
    bool encryptSections = false;
    bool fakeInstructions = false;
    bool packFunctions = false;
    bool outputCustomized = false;
    int logAnimationGeneration = 0;
    bool inputShowFullPath = false;
    bool outputShowFullPath = false;
    std::vector<std::pair<std::string, bool>> logEntries;
    FileInfo fileInfo;
};

static ID3D11Device*           g_pd3dDevice = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*         g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// drag n drop state
static HWND g_hwnd = nullptr;
static GuiState* g_state = nullptr;
static std::string g_droppedFilePath;
static bool g_dropToInput = false;
static bool g_hasDroppedFile = false;
static ImVec2 g_inputFieldMin = ImVec2(0, 0);
static ImVec2 g_inputFieldMax = ImVec2(0, 0);
static ImVec2 g_outputFieldMin = ImVec2(0, 0);
static ImVec2 g_outputFieldMax = ImVec2(0, 0);
static ImVec2 g_minBtnMin = ImVec2(0, 0);
static ImVec2 g_minBtnMax = ImVec2(0, 0);
static ImVec2 g_closeBtnMin = ImVec2(0, 0);
static ImVec2 g_closeBtnMax = ImVec2(0, 0);
static ImVec2 g_titleIconsMin = ImVec2(0, 0);
static ImVec2 g_titleIconsMax = ImVec2(0, 0);
static ImVec2 g_titleIconsClientMin = ImVec2(0, 0);
static ImVec2 g_titleIconsClientMax = ImVec2(0, 0);
static bool g_shouldClose = false;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static bool ExecutePacking(GuiState& state, std::string& statusMessage);
static void RenderGui(GuiState& state, std::string& statusMessage, bool& lastSuccess);
static bool BrowseInputFile(GuiState& state);
static bool BrowseOutputFile(GuiState& state);
static bool AnimatedCheckbox(const char* label, bool* value);
static bool AnimatedIntSlider(const char* label, int* value, int minValue, int maxValue, float width = 0.0f);
static bool GradientButton(
    const char* label,
    const ImVec2& size,
    ImVec4 baseL,
    ImVec4 baseR,
    ImVec4 hoverL,
    ImVec4 hoverR,
    float rounding_override = -1.0f,
    bool uniform_fill = false,
    bool accent_glow = true);
static void DrawPanelBorder(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, float rounding);
static void DrawPanelTitle(const char* title, const ImVec2& panel_min, float panel_width);
static void BeginPanelContent();
static float PanelContentWidth();
static void AlignLabelLeft(const char* label);
static void VCenterInRow(float row_h);
static void RenderFileInfoList(const FileInfo& info, float content_w, float row_h);
static std::string FitTextToWidth(const char* text, float max_width);
static void SetDefaultOutputFromInput(GuiState& state);
static void CopyStringToBuffer(const std::string& value, std::array<char, MAX_PATH>& buffer);
static void CopyShortStringToBuffer(const std::string& value, std::array<char, MAX_PATH>& buffer);
static void ApplyBorderlessWindow(HWND hwnd, int width, int height);
static void ApplyCustomTheme(ImGuiStyle& style);
static void LoadUiFonts(ImGuiIO& io);
static void ExtractFileInfo(GuiState& state);

int run_gui() {
    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX),
        CS_CLASSDC,
        WndProc,
        0L,
        0L,
        GetModuleHandle(nullptr),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        _T("pe-packer-gui"),
        nullptr
    };
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindow(
        wc.lpszClassName,
        _T("pe-packer"),
        WS_POPUP,
        100, 100,
        kGuiWidth, kGuiHeight,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr);

    ApplyBorderlessWindow(hwnd, kGuiWidth, kGuiHeight);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return EXIT_FAILURE;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // enable drag and drop for files
    DragAcceptFiles(hwnd, TRUE);
    g_hwnd = hwnd;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyCustomTheme(ImGui::GetStyle());
    LoadUiFonts(io);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    GuiState state{};
    g_state = &state;
    std::string statusMessage;
    bool lastSuccess = false;

    MSG msg{};
    bool done = false;
    while (!done) {
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
        if (done || g_shouldClose) {
            break;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderGui(state, statusMessage, lastSuccess);

        ImGui::Render();
        ImVec4 windowColor = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float clear_color_with_alpha[4] = { windowColor.x, windowColor.y, windowColor.z, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return EXIT_SUCCESS;
}

bool ExecutePacking(GuiState& state, std::string& statusMessage) {
    std::string inputPath(state.input.data());
    std::string outputPath(state.output.data());
    std::string fpackStart(state.fpackStart.data());
    std::string fpackEnd(state.fpackEnd.data());

    if (inputPath.empty()) {
        statusMessage = "Input file path is required.";
        return false;
    }
    if (outputPath.empty()) {
        statusMessage = "Output file path is required.";
        return false;
    }
    if (state.mutationBase < 1) {
        statusMessage = "Mutations must be at least 1.";
        return false;
    }
    if (state.packFunctions && (fpackStart.empty() || fpackEnd.empty())) {
        statusMessage = "Provide both start and end addresses for -fpack.";
        return false;
    }

    // clear previous logs before each new run and force fresh animation IDs
    state.logEntries.clear();
    ++state.logAnimationGeneration;

    g_logCallback = [&state](const std::string& logMsg, bool isSuccess) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        std::ostringstream line;
        line << "[" << std::setfill('0') << std::setw(2) << st.wHour << ":"
             << std::setfill('0') << std::setw(2) << st.wMinute << ":"
             << std::setfill('0') << std::setw(2) << st.wSecond << "] "
             << logMsg;
        state.logEntries.emplace_back(line.str(), isSuccess);
        if (state.logEntries.size() > 100) {
            state.logEntries.erase(state.logEntries.begin());
        }
    };

    std::vector<std::string> args;
    args.emplace_back("pe-packer");
    args.emplace_back(inputPath);
    args.emplace_back(outputPath);
    args.emplace_back(std::to_string(state.mutationBase));

    auto append_flag = [&args](bool enabled, const char* flag) {
        if (enabled) {
            args.emplace_back(flag);
        }
    };

    append_flag(state.removeAslr, "-noaslr");
    append_flag(state.obfuscateOep, "-oep_call");
    append_flag(state.antiDisasm, "-adasm");
    append_flag(state.mba, "-mba");
    append_flag(state.encryptSections, "-senc");
    append_flag(state.fakeInstructions, "-finstr");

    if (state.packFunctions) {
        args.emplace_back("-fpack");
        args.emplace_back(fpackStart);
        args.emplace_back(fpackEnd);
    }

    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.data()));
    }

    arguments::init(static_cast<int>(argv.size()), argv.data());

    try {
        const auto obfuscation_level = mutation_profile::level_from_slider(
            static_cast<uint32_t>(state.mutationBase)
        );
        print_info("Complexity percentage level is: %u\n", obfuscation_level, state.mutationBase);
        auto packer = std::make_unique<c_core>(inputPath, outputPath, obfuscation_level);
        packer->process();
        
        if (state.logEntries.size() > 100) {
            state.logEntries.erase(state.logEntries.begin());
        }
        
        // reset callback
        g_logCallback = nullptr;
        return true;
    }
    catch (const std::exception& ex) {
        statusMessage = ex.what();
        
        // add message error
        SYSTEMTIME st{};
        GetLocalTime(&st);
        std::ostringstream line;
        line << "[" << std::setfill('0') << std::setw(2) << st.wHour << ":"
             << std::setfill('0') << std::setw(2) << st.wMinute << ":"
             << std::setfill('0') << std::setw(2) << st.wSecond << "] "
             << "[ error ] " << ex.what();
        state.logEntries.emplace_back(line.str(), false);
        if (state.logEntries.size() > 100) {
            state.logEntries.erase(state.logEntries.begin());
        }
        
        g_logCallback = nullptr;
        return false;
    }
}

bool AnimatedCheckbox(const char* label, bool* value) {
    ImGui::PushID(label);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float frame_h = ImGui::GetFrameHeight();
    const float box_size = frame_h * 0.72f;
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
    const float spacing = style.ItemInnerSpacing.x;
    float total_w = box_size + (label_size.x > 0.0f ? spacing + label_size.x : 0.0f);
    const float avail_w = ImGui::GetContentRegionAvail().x;
    if (avail_w > total_w) {
        total_w = avail_w;
    }
    const float total_h = frame_h;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##toggle", ImVec2(total_w, total_h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImGuiID id = ImGui::GetItemID();

    if (pressed) {
        *value = !*value;
    }

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID checkAnimId = id;
    const ImGuiID hoverAnimId = id ^ 0x7F4A2B1Du;
    const ImGuiID bounceAnimId = id ^ 0x3C8E5F90u;
    const ImGuiID rippleAnimId = id ^ 0xA1B2C3D4u;

    float check_anim = storage->GetFloat(checkAnimId, *value ? 1.0f : 0.0f);
    float hover_anim = storage->GetFloat(hoverAnimId, 0.0f);
    float bounce = storage->GetFloat(bounceAnimId, 0.0f);
    float ripple = storage->GetFloat(rippleAnimId, 0.0f);

    const float dt = ImGui::GetIO().DeltaTime;
    const float check_target = *value ? 1.0f : 0.0f;
    const float check_speed = pressed ? 22.0f : 14.0f;
    check_anim += (check_target - check_anim) * (std::min)(1.0f, dt * check_speed);

    const float hover_target = hovered ? 1.0f : 0.0f;
    hover_anim += (hover_target - hover_anim) * (std::min)(1.0f, dt * 16.0f);

    if (pressed) {
        bounce = 1.0f;
        ripple = 1.0f;
    }
    bounce = (std::max)(0.0f, bounce - dt * 4.8f);
    ripple = (std::max)(0.0f, ripple - dt * 3.2f);

    storage->SetFloat(checkAnimId, check_anim);
    storage->SetFloat(hoverAnimId, hover_anim);
    storage->SetFloat(bounceAnimId, bounce);
    storage->SetFloat(rippleAnimId, ripple);

    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    auto lerp = [&](float a, float b, float t) { return a + (b - a) * clamp01(t); };
    auto lerp4 = [&](const ImVec4& a, const ImVec4& b, float t) {
        t = clamp01(t);
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    };
    auto ease_out_cubic = [&](float t) {
        t = clamp01(t);
        const float u = 1.0f - t;
        return 1.0f - u * u * u;
    };
    auto ease_out_back = [&](float t) {
        t = clamp01(t);
        const float c1 = 1.70158f;
        const float c3 = c1 + 1.0f;
        return 1.0f + c3 * (t - 1.0f) * (t - 1.0f) * (t - 1.0f) + c1 * (t - 1.0f) * (t - 1.0f);
    };

    const float box_y = pos.y + (total_h - box_size) * 0.5f;
    const ImVec2 box_min(pos.x, box_y);
    const ImVec2 box_max(pos.x + box_size, box_y + box_size);
    const ImVec2 box_center((box_min.x + box_max.x) * 0.5f, (box_min.y + box_max.y) * 0.5f);
    const float rounding = box_size * 0.30f;

    const float bounce_scale = 1.0f + bounce * 0.10f * std::sinf(bounce * 3.14159265f);
    const float draw_size = box_size * bounce_scale;
    const ImVec2 draw_min(box_center.x - draw_size * 0.5f, box_center.y - draw_size * 0.5f);
    const ImVec2 draw_max(box_center.x + draw_size * 0.5f, box_center.y + draw_size * 0.5f);

    const ImVec4 bg_off(kClrSurface.x, kClrSurface.y, kClrSurface.z, 1.0f);
    const ImVec4 bg_on(kClrAccentActive.x, kClrAccentActive.y, kClrAccentActive.z, 1.0f);
    const ImVec4 border_off(kClrTextDim.x, kClrTextDim.y, kClrTextDim.z, 0.55f + hover_anim * 0.25f);
    const ImVec4 border_on(kClrAccentHover.x, kClrAccentHover.y, kClrAccentHover.z, 0.95f);
    const float fill_t = ease_out_cubic(check_anim);
    const ImVec4 bg = lerp4(bg_off, bg_on, fill_t);
    const ImVec4 border = lerp4(border_off, border_on, check_anim);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (hover_anim > 0.01f) {
        const float glow_expand = 3.0f + hover_anim * 2.0f;
        draw_list->AddRectFilled(
            ImVec2(draw_min.x - glow_expand, draw_min.y - glow_expand),
            ImVec2(draw_max.x + glow_expand, draw_max.y + glow_expand),
            ImGui::GetColorU32(ImVec4(kClrAccent.x, kClrAccent.y, kClrAccent.z, 0.07f + hover_anim * 0.06f)),
            rounding + glow_expand);
    }

    if (ripple > 0.01f) {
        const float ripple_t = 1.0f - ripple;
        const float ripple_r = draw_size * (0.35f + ripple_t * 0.75f);
        draw_list->AddCircleFilled(
            box_center,
            ripple_r,
            ImGui::GetColorU32(ImVec4(kClrAccentHover.x, kClrAccentHover.y, kClrAccentHover.z, 0.14f * ripple)),
            32);
    }

    if (active) {
        draw_list->AddRectFilled(draw_min, draw_max, ImGui::GetColorU32(ImVec4(bg.x * 0.92f, bg.y * 0.92f, bg.z * 0.92f, bg.w)), rounding);
    } else {
        draw_list->AddRectFilled(draw_min, draw_max, ImGui::GetColorU32(bg), rounding);
    }

    const float border_thickness = lerp(1.1f, 1.6f, check_anim) + hover_anim * 0.35f;
    draw_list->AddRect(draw_min, draw_max, ImGui::GetColorU32(border), rounding, 0, border_thickness);

    if (check_anim > 0.001f) {
        const float mark_t = ease_out_back(check_anim);
        const float pad = draw_size * 0.20f;
        const ImVec2 p1(draw_min.x + draw_size * 0.24f, draw_min.y + draw_size * 0.52f);
        const ImVec2 p2(draw_min.x + draw_size * 0.43f, draw_min.y + draw_size * 0.72f);
        const ImVec2 p3(draw_max.x - pad, draw_min.y + pad);

        auto scale_pt = [&](const ImVec2& p) {
            return ImVec2(box_center.x + (p.x - box_center.x) * mark_t, box_center.y + (p.y - box_center.y) * mark_t);
        };

        const float thickness = (std::max)(1.8f, draw_size * 0.12f);
        const ImU32 check_col = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.95f * check_anim));
        draw_list->AddLine(scale_pt(p1), scale_pt(p2), check_col, thickness);
        draw_list->AddLine(scale_pt(p2), scale_pt(p3), check_col, thickness);
    }

    if (label_size.x > 0.0f) {
        const ImVec4 text_base = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const ImVec4 text_hover(kClrText.x + 0.08f, kClrText.y + 0.05f, kClrText.z + 0.06f, 1.0f);
        ImVec4 text_col = lerp4(text_base, text_hover, hover_anim * 0.65f + check_anim * 0.15f);
        if (*value) {
            text_col.w = 1.0f;
        }
        const ImVec2 label_pos(pos.x + box_size + spacing, pos.y + (total_h - label_size.y) * 0.5f);
        draw_list->AddText(label_pos, ImGui::GetColorU32(text_col), label);
    }

    ImGui::PopID();
    return pressed;
}

bool AnimatedIntSlider(const char* label, int* value, int minValue, int maxValue, float width) {
    ImGui::PushID(label);

    char valueText[32];
    std::snprintf(valueText, sizeof(valueText), "%d%%", *value);
    const ImVec2 valueTextSize = ImGui::CalcTextSize(valueText);
    const float content_width = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginTable(
            "##slider_hdr",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX,
            ImVec2(content_width, 0.0f))) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, valueTextSize.x + 4.0f);
        ImGui::TableNextRow(0, ImGui::GetTextLineHeight());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        const float col_w = ImGui::GetColumnWidth();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + col_w - valueTextSize.x);
        ImGui::TextDisabled("%s", valueText);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::SetCursorPosX(kPanelPad);
    ImGui::SetNextItemWidth(content_width);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool changed = ImGui::SliderInt("##animated_slider", value, minValue, maxValue, "%d");
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();

    const ImVec2 frameMin = ImGui::GetItemRectMin();
    const ImVec2 frameMax = ImGui::GetItemRectMax();
    const float height = frameMax.y - frameMin.y;

    const float denom = static_cast<float>((std::max)(1, maxValue - minValue));
    const float targetT = static_cast<float>(*value - minValue) / denom;

    ImGuiID id = ImGui::GetItemID();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID animId = id ^ 0x4B91A2D3u;
    float smoothT = storage->GetFloat(animId, targetT);
    const float speed = ImGui::IsItemActive() ? 22.0f : 12.0f;
    const float lerpFactor = (std::min)(1.0f, ImGui::GetIO().DeltaTime * speed);
    smoothT = smoothT + (targetT - smoothT) * lerpFactor;
    storage->SetFloat(animId, smoothT);

    const float visualHeight = height * 0.52f;
    const float rounding = visualHeight * 0.5f;
    const float knobRadius = visualHeight * 0.42f;
    const float xPad = knobRadius + 2.0f;
    const float leftX = frameMin.x + xPad;
    const float rightX = frameMax.x - xPad;
    const float knobX = leftX + (rightX - leftX) * smoothT;
    const float centerY = (frameMin.y + frameMax.y) * 0.5f;
    const ImVec2 trackMin(frameMin.x, centerY - visualHeight * 0.5f);
    const ImVec2 trackMax(frameMax.x, centerY + visualHeight * 0.5f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(trackMin, trackMax, ImGui::GetColorU32(kClrSurface), rounding);
    draw->AddRectFilled(trackMin, ImVec2(knobX, trackMax.y), ImGui::GetColorU32(kClrAccent), rounding);

    const ImU32 knobColor = ImGui::GetColorU32(
        ImGui::IsItemActive() ? kClrSliderKnobActive : kClrSliderKnob);
    draw->AddCircleFilled(ImVec2(knobX, centerY), knobRadius, knobColor, 24);
    draw->AddCircle(ImVec2(knobX, centerY), knobRadius, ImGui::GetColorU32(ImVec4(0.06f, 0.07f, 0.08f, 0.55f)), 24, 1.0f);

    ImGui::PopID();
    return changed;
}

// especially buttons :D but need to improve
static bool GradientButton(
    const char* label,
    const ImVec2& size,
    ImVec4 baseL,
    ImVec4 baseR,
    ImVec4 hoverL,
    ImVec4 hoverR,
    float rounding_override,
    bool uniform_fill,
    bool accent_glow) {
    ImGui::PushID(label);
    const ImGuiID id = ImGui::GetID("##grad_btn");

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 s = size;
    if (s.x <= 0.0f) s.x = ImGui::CalcItemWidth();
    if (s.y <= 0.0f) s.y = ImGui::GetFrameHeight();

    const bool pressed = ImGui::InvisibleButton("##grad_btn", s);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID animId = id ^ 0xA8D3C2F1u;
    const ImGuiID pressAnimId = id ^ 0xC0FFEE01u;
    const float dt = ImGui::GetIO().DeltaTime;

    float t = st->GetFloat(animId, hovered ? 1.0f : 0.0f);
    const float hover_target = hovered ? 1.0f : 0.0f;
    const float hover_speed = 12.0f;
    t += (hover_target - t) * (std::min)(1.0f, dt * hover_speed);
    st->SetFloat(animId, t);

    float press_t = st->GetFloat(pressAnimId, 0.0f);
    const float press_target = active ? 1.0f : 0.0f;
    const float press_speed = active ? 24.0f : 18.0f;
    press_t += (press_target - press_t) * (std::min)(1.0f, dt * press_speed);
    st->SetFloat(pressAnimId, press_t);

    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    auto mix = [&](const ImVec4& a, const ImVec4& b, float x) -> ImVec4 {
        x = clamp01(x);
        return ImVec4(a.x + (b.x - a.x) * x, a.y + (b.y - a.y) * x, a.z + (b.z - a.z) * x, 1.0f);
    };

    const ImVec4 baseMid((baseL.x + baseR.x) * 0.5f, (baseL.y + baseR.y) * 0.5f, (baseL.z + baseR.z) * 0.5f, 1.0f);
    const ImVec4 hoverMid((hoverL.x + hoverR.x) * 0.5f, (hoverL.y + hoverR.y) * 0.5f, (hoverL.z + hoverR.z) * 0.5f, 1.0f);
    ImVec4 mid = mix(baseMid, hoverMid, t);
    const float press_shade = 1.0f - press_t * 0.10f;
    mid.x *= press_shade;
    mid.y *= press_shade;
    mid.z *= press_shade;

    const float inset = press_t * 1.5f;
    const ImVec2 draw_pos(pos.x + inset, pos.y + inset);
    const ImVec2 draw_p2(pos.x + s.x - inset, pos.y + s.y - inset);
    const float draw_h = draw_p2.y - draw_pos.y;
    const float rounding = rounding_override >= 0.0f ? rounding_override : draw_h * 0.45f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(draw_pos, draw_p2, ImGui::GetColorU32(mid), rounding);

    if (!uniform_fill) {
        ImVec4 top = mid;
        top.x = clamp01(top.x + 0.10f * t);
        top.y = clamp01(top.y + 0.10f * t);
        top.z = clamp01(top.z + 0.14f * t);
        top.w = 0.26f + 0.26f * t;
        dl->AddRectFilled(draw_pos, ImVec2(draw_p2.x, draw_pos.y + draw_h * 0.54f), ImGui::GetColorU32(top), rounding, ImDrawFlags_RoundCornersTop);

        ImVec4 bot = mid;
        bot.w = 0.22f + 0.18f * t;
        dl->AddRectFilled(ImVec2(draw_pos.x, draw_pos.y + draw_h * 0.56f), draw_p2,
            ImGui::GetColorU32(ImVec4(bot.x * 0.75f, bot.y * 0.75f, bot.z * 0.75f, bot.w)),
            rounding,
            ImDrawFlags_RoundCornersBottom);
    } else if (t > 0.01f) {
        if (accent_glow) {
            dl->AddRectFilled(
                draw_pos,
                draw_p2,
                ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.10f * t)),
                rounding);
        } else {
            dl->AddRectFilled(
                draw_pos,
                draw_p2,
                ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.09f * t)),
                rounding);
        }
    }

    const float border_alpha = accent_glow ? (0.10f + 0.25f * t) : (0.08f + 0.14f * t);
    dl->AddRect(draw_pos, draw_p2, ImGui::GetColorU32(ImVec4(1, 1, 1, border_alpha)), rounding, 0, 1.0f);
    if (t > 0.01f && accent_glow) {
        const float glow_expand = uniform_fill ? 1.0f : 3.0f;
        dl->AddRect(
            ImVec2(draw_pos.x - glow_expand, draw_pos.y - glow_expand),
            ImVec2(draw_p2.x + glow_expand, draw_p2.y + glow_expand),
            ImGui::GetColorU32(ImVec4(kClrAccent.x, kClrAccent.y, kClrAccent.z, (uniform_fill ? 0.18f : 0.12f) * t)),
            rounding + glow_expand,
            0,
            uniform_fill ? 1.6f : 2.4f);
        if (!uniform_fill) {
            dl->AddRect(
                ImVec2(draw_pos.x - 3.0f, draw_pos.y - 3.0f),
                ImVec2(draw_p2.x + 3.0f, draw_p2.y + 3.0f),
                ImGui::GetColorU32(ImVec4(kClrAccentHover.x, kClrAccentHover.y, kClrAccentHover.z, 0.06f * t)),
                rounding + 3.0f,
                0,
                3.0f);
        }
    }

    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float text_y = draw_pos.y + (draw_h - ts.y) * 0.5f - 1.0f + press_t * 0.5f;
    const ImVec2 tp(draw_pos.x + (draw_p2.x - draw_pos.x - ts.x) * 0.5f, text_y);
    dl->AddText(tp, ImGui::GetColorU32(accent_glow ? ImVec4(0.96f, 0.97f, 0.98f, 1.0f) : kClrText), label);

    ImGui::PopID();
    return pressed;
}

void DrawPanelBorder(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, float rounding) {
    draw_list->AddRect(
        min,
        max,
        ImGui::ColorConvertFloat4ToU32(kClrBorderSubtle),
        rounding,
        0,
        1.0f);
}

void DrawPanelTitle(const char* title, const ImVec2& panel_min, float panel_width) {
    const ImVec2 title_size = ImGui::CalcTextSize(title);
    const float title_x = panel_min.x + (panel_width - title_size.x) * 0.5f;
    const float title_y = panel_min.y - title_size.y * 0.5f + 2.0f;
    const ImU32 child_bg = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
    const ImU32 title_col = ImGui::GetColorU32(kClrTextTitle);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float cover_pad_x = 12.0f;
    const float cover_pad_y = 4.0f;
    draw_list->AddRectFilled(
        ImVec2(title_x - cover_pad_x, title_y - cover_pad_y),
        ImVec2(title_x + title_size.x + cover_pad_x, title_y + title_size.y + cover_pad_y),
        child_bg);

    ImGui::GetForegroundDrawList()->AddText(ImVec2(title_x, title_y), title_col, title);

    ImGui::Dummy(ImVec2(0.0f, title_size.y * 0.5f + kPanelTitleGap));
}

void BeginPanelContent() {
    ImGui::SetCursorPosX(kPanelPad);
}

float PanelContentWidth() {
    return ImGui::GetWindowSize().x - kPanelPad * 2.0f;
}

void AlignLabelLeft(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

void VCenterInRow(float row_h) {
    const float frame_h = ImGui::GetFrameHeight();
    if (row_h > frame_h) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (row_h - frame_h) * 0.5f);
    }
}

std::string FitTextToWidth(const char* text, float max_width) {
    if (!text || text[0] == '\0' || max_width <= 1.0f) {
        return text ? text : "";
    }
    if (ImGui::CalcTextSize(text).x <= max_width) {
        return text;
    }

    std::string fitted(text);
    const char* ellipsis = "...";
    while (!fitted.empty()) {
        fitted.pop_back();
        if (ImGui::CalcTextSize((fitted + ellipsis).c_str()).x <= max_width) {
            fitted += ellipsis;
            break;
        }
    }
    return fitted.empty() ? "..." : fitted;
}

void RenderFileInfoList(const FileInfo& info, float content_w, float row_h) {
    const ImVec4 arch_color = kClrAccent;

    struct InfoRow {
        const char* label;
        const char* value;
        const ImVec4* color;
    };

    const InfoRow rows[] = {
        {"File", info.filePath.c_str(), nullptr},
        {"Architecture", info.architecture.c_str(), &arch_color},
        {"Image Base", info.imageBase.c_str(), nullptr},
        {"Image Size", info.imageSize.c_str(), nullptr},
        {"Entry Point", info.entryPoint.c_str(), nullptr},
        {"Sections", info.sectionCount.c_str(), nullptr},
    };

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 5.0f));
    if (ImGui::BeginTable(
            "##file_info",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX,
            ImVec2(content_w, 0.0f))) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, kInfoLabelWidth);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        for (const InfoRow& row : rows) {
            ImGui::TableNextRow(0, row_h);
            ImGui::TableNextColumn();
            VCenterInRow(row_h);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", row.label);
            ImGui::TableNextColumn();
            VCenterInRow(row_h);
            ImGui::AlignTextToFramePadding();
            const std::string fitted = FitTextToWidth(row.value, ImGui::GetContentRegionAvail().x);
            if (row.color) {
                ImGui::PushStyleColor(ImGuiCol_Text, *row.color);
            }
            ImGui::TextUnformatted(fitted.c_str());
            if (row.color) {
                ImGui::PopStyleColor();
            }
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

void RenderGui(GuiState& state, std::string& statusMessage, bool& lastSuccess) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("pe-packer GUI", nullptr, windowFlags)) {
        // title bar
        ImGui::PushStyleColor(ImGuiCol_Text, kClrTextTitle);
        ImGui::Text("pe-packer");
        const ImVec2 titleTextMin = ImGui::GetItemRectMin();
        const ImVec2 titleTextMax = ImGui::GetItemRectMax();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("|  Version 1.0.5");
        const ImVec2 versionTextMin = ImGui::GetItemRectMin();
        const ImVec2 versionTextMax = ImGui::GetItemRectMax();
        
        // title bar icons
        const float iconSize = 20.0f;
        const float iconSpacing = 10.0f;
        const float rightPadding = 6.0f;
        float windowRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
        float btnRowWidth = iconSize * 2.0f + iconSpacing;
        const float lineMinY = (std::min)(titleTextMin.y, versionTextMin.y);
        const float lineMaxY = (std::max)(titleTextMax.y, versionTextMax.y);
        const float lineH = lineMaxY - lineMinY;
        const float iconsY = lineMinY + (lineH - iconSize) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(windowRight - rightPadding - btnRowWidth, iconsY));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kClrPanel.x + 0.02f, kClrPanel.y + 0.02f, kClrPanel.z + 0.01f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(kClrAccent.x, kClrAccent.y, kClrAccent.z, 0.20f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(kClrAccentActive.x, kClrAccentActive.y, kClrAccentActive.z, 0.30f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        
        if (ImGui::Button("##min_btn", ImVec2(iconSize, iconSize))) {
            ShowWindow(g_hwnd, SW_MINIMIZE);
        }
        g_minBtnMin = ImGui::GetItemRectMin();
        g_minBtnMax = ImGui::GetItemRectMax();
        {
            ImVec2 bmin = ImGui::GetItemRectMin();
            ImVec2 bmax = ImGui::GetItemRectMax();
            float cx = (bmin.x + bmax.x) * 0.5f;
            float cy = (bmin.y + bmax.y) * 0.5f;
            float lineHalf = iconSize * 0.22f;
            bool hov = ImGui::IsItemHovered();
            bool act = ImGui::IsItemActive();
            ImVec4 col = hov || act ? kClrAccentHover : kClrTextTitle;
            ImU32 colorU32 = ImGui::GetColorU32(col);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddLine(ImVec2(cx - lineHalf, cy), ImVec2(cx + lineHalf, cy), colorU32, 2.0f);
        }

        ImGui::SameLine(0.0f, iconSpacing);
        if (ImGui::Button("##close_btn", ImVec2(iconSize, iconSize))) {
            g_shouldClose = true;
            PostQuitMessage(0);
        }
        g_closeBtnMin = ImGui::GetItemRectMin();
        g_closeBtnMax = ImGui::GetItemRectMax();
        g_titleIconsMin = ImVec2((std::min)(g_minBtnMin.x, g_closeBtnMin.x), (std::min)(g_minBtnMin.y, g_closeBtnMin.y));
        g_titleIconsMax = ImVec2((std::max)(g_minBtnMax.x, g_closeBtnMax.x), (std::max)(g_minBtnMax.y, g_closeBtnMax.y));
        {
            POINT pMin{ static_cast<LONG>(g_titleIconsMin.x), static_cast<LONG>(g_titleIconsMin.y) };
            POINT pMax{ static_cast<LONG>(g_titleIconsMax.x), static_cast<LONG>(g_titleIconsMax.y) };
            ScreenToClient(g_hwnd, &pMin);
            ScreenToClient(g_hwnd, &pMax);
            g_titleIconsClientMin = ImVec2(static_cast<float>(pMin.x), static_cast<float>(pMin.y));
            g_titleIconsClientMax = ImVec2(static_cast<float>(pMax.x), static_cast<float>(pMax.y));
        }
        {
            ImVec2 bmin = ImGui::GetItemRectMin();
            ImVec2 bmax = ImGui::GetItemRectMax();
            float pad = iconSize * 0.24f;
            bool hov = ImGui::IsItemHovered();
            bool act = ImGui::IsItemActive();
            ImVec4 col = hov || act ? kClrLogErr : kClrTextTitle;
            ImU32 colorU32 = ImGui::GetColorU32(col);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddLine(ImVec2(bmin.x + pad, bmin.y + pad), ImVec2(bmax.x - pad, bmax.y - pad), colorU32, 2.0f);
            dl->AddLine(ImVec2(bmin.x + pad, bmax.y - pad), ImVec2(bmax.x - pad, bmin.y + pad), colorU32, 2.0f);
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        
        ImGui::Spacing();

        ImGuiStyle& style = ImGui::GetStyle();

        bool inputEdited = false;
        bool outputEdited = false;
        const float path_row_h = ImGui::GetFrameHeight();
        const float pack_block_h = path_row_h * 2.0f;
        ImVec2 browse_top_min(0.0f, 0.0f);
        ImVec2 browse_bottom_max(0.0f, 0.0f);

        auto drawPathField = [&](std::array<char, MAX_PATH>& fullBuffer,
                                  std::array<char, MAX_PATH>& displayBuffer,
                                  bool isInput,
                                  auto&& browseFn) -> bool {
            ImVec2 fieldMin = ImGui::GetCursorScreenPos();
            const float fieldHeight = ImGui::GetFrameHeight();
            ImVec2 fieldMaxGuess(fieldMin.x + ImGui::GetColumnWidth(), fieldMin.y + fieldHeight);

            bool showFull = isInput ? state.inputShowFullPath : state.outputShowFullPath;
            if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(fieldMin, fieldMaxGuess, true)) {
                showFull = true;
            }

            if (!showFull) {
                CopyShortStringToBuffer(std::string(fullBuffer.data()), displayBuffer);
            }

            bool edited = false;
            if (g_hasDroppedFile && g_dropToInput == isInput) {
                CopyStringToBuffer(g_droppedFilePath, fullBuffer);
                if (isInput) {
                    state.outputCustomized = false;
                } else {
                    state.outputCustomized = true;
                }
                if (!showFull) {
                    CopyShortStringToBuffer(g_droppedFilePath, displayBuffer);
                }
                edited = true;
                g_hasDroppedFile = false;
            }

            const ImGuiInputTextFlags inputFlags = showFull ? 0 : ImGuiInputTextFlags_ReadOnly;
            char* inputPtr = showFull ? fullBuffer.data() : displayBuffer.data();
            ImGui::SetNextItemWidth(-FLT_MIN);
            edited = ImGui::InputText("##path", inputPtr, static_cast<int>(fullBuffer.size()), inputFlags) || edited;

            const float inputHeight = ImGui::GetItemRectSize().y;
            ImVec2 fieldMax = ImGui::GetItemRectMax();
            if (isInput) {
                g_inputFieldMin = fieldMin;
                g_inputFieldMax = fieldMax;
                state.inputShowFullPath = ImGui::IsItemActive();
            } else {
                g_outputFieldMin = fieldMin;
                g_outputFieldMax = fieldMax;
                state.outputShowFullPath = ImGui::IsItemActive();
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE")) {
                    (void)payload;
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::TableNextColumn();
            ImGui::Dummy(ImVec2(kBrowseGap, 1.0f));
            ImGui::TableNextColumn();
            if (GradientButton(
                    "Browse",
                    ImVec2(kBrowseBtnWidth, inputHeight),
                    kClrBtnNeutralLo,
                    kClrBtnNeutralHi,
                    kClrSurfaceHi,
                    ImVec4(kClrSurfaceHi.x + 0.03f, kClrSurfaceHi.y + 0.03f, kClrSurfaceHi.z + 0.02f, 1.0f),
                    8.0f,
                    true,
                    false) && browseFn()) {
                edited = true;
            }
            if (isInput) {
                browse_top_min = ImGui::GetItemRectMin();
            } else {
                browse_bottom_max = ImGui::GetItemRectMax();
            }
            return edited;
        };

        if (ImGui::BeginTable(
                "FilePathsLayout",
                3,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX,
                ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
            ImGui::TableSetupColumn("paths", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("gap", ImGuiTableColumnFlags_WidthFixed, kBrowseGap);
            ImGui::TableSetupColumn("pack", ImGuiTableColumnFlags_WidthFixed, kPackBtnWidth);

            ImGui::TableNextRow(0, pack_block_h);
            ImGui::TableNextColumn();

            if (ImGui::BeginTable("FilePaths", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadInnerX)) {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, kFormLabelWidth);
                ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("gap", ImGuiTableColumnFlags_WidthFixed, kBrowseGap);
                ImGui::TableSetupColumn("browse", ImGuiTableColumnFlags_WidthFixed, kBrowseBtnWidth);

                ImGui::TableNextRow(0, path_row_h);
                ImGui::TableNextColumn();
                VCenterInRow(path_row_h);
                AlignLabelLeft("Input file");
                ImGui::TableNextColumn();
                VCenterInRow(path_row_h);
                ImGui::PushID("input");
                inputEdited = drawPathField(state.input, state.inputDisplay, true, [&]() { return BrowseInputFile(state); });
                ImGui::PopID();

                ImGui::TableNextRow(0, path_row_h);
                ImGui::TableNextColumn();
                VCenterInRow(path_row_h);
                AlignLabelLeft("Output file");
                ImGui::TableNextColumn();
                VCenterInRow(path_row_h);
                ImGui::PushID("output");
                outputEdited = drawPathField(state.output, state.outputDisplay, false, [&]() { return BrowseOutputFile(state); });
                ImGui::PopID();

                ImGui::EndTable();
            }

            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            const float pack_btn_h = browse_bottom_max.y - browse_top_min.y;
            const ImVec2 pack_col_pos = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(pack_col_pos.x, browse_top_min.y));
            if (GradientButton(
                    "Pack",
                    ImVec2(kPackBtnWidth, pack_btn_h),
                    kClrPackLo,
                    kClrPackHi,
                    kClrPackHoverLo,
                    kClrPackHoverHi,
                    8.0f,
                    true,
                    true)) {
                lastSuccess = ExecutePacking(state, statusMessage);
            }

            ImGui::EndTable();
        }

        if (inputEdited) {
            SetDefaultOutputFromInput(state);
            ExtractFileInfo(state);
        }
        if (outputEdited) {
            state.outputCustomized = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // reserve space for logs at the bottom
        float middleSectionHeight = ImGui::GetContentRegionAvail().y - kLogsHeight - style.ItemSpacing.y;
        if (middleSectionHeight < 80.0f) {
            middleSectionHeight = 80.0f;
        }

        const float columns_avail = ImGui::GetContentRegionAvail().x;
        const float column_gap = style.ItemSpacing.x;
        const float leftColumnWidth = (columns_avail - column_gap) * 0.52f;
        const float rightColumnWidth = columns_avail - column_gap - leftColumnWidth;
        const float option_row_h = ImGui::GetFrameHeight();
        const ImVec2 panel_child_pad(8.0f, 8.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, panel_child_pad);
        if (ImGui::BeginChild("OptionsColumn", ImVec2(leftColumnWidth, middleSectionHeight), false, kPanelChildFlags)) {
            const float panel_w = ImGui::GetWindowSize().x;
            const ImVec2 col_min = ImGui::GetWindowPos();
            const ImVec2 col_max(col_min.x + panel_w, col_min.y + ImGui::GetWindowSize().y);
            DrawPanelBorder(ImGui::GetWindowDrawList(), col_min, col_max, 12.0f);
            DrawPanelTitle("Options", col_min, panel_w);

            const float content_w = PanelContentWidth();
            BeginPanelContent();
            AnimatedIntSlider("Complexity percentage", &state.mutationBase, 1, 100, content_w);

            BeginPanelContent();
            const float checkbox_cell_pad_y = 2.0f;
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, checkbox_cell_pad_y));
            if (ImGui::BeginTable(
                    "OptionsFlags",
                    3,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX,
                    ImVec2(content_w, 0.0f))) {
                ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("gap", ImGuiTableColumnFlags_WidthFixed, kOptionsColumnGap);
                ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch);

                auto option_pair = [&](const char* left_label, bool* left_value, const char* right_label, bool* right_value) {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, option_row_h);
                    ImGui::TableNextColumn();
                    VCenterInRow(option_row_h);
                    AnimatedCheckbox(left_label, left_value);
                    ImGui::TableNextColumn();
                    ImGui::TableNextColumn();
                    VCenterInRow(option_row_h);
                    AnimatedCheckbox(right_label, right_value);
                };

                option_pair("Remove ASLR", &state.removeAslr, "OEP call obfuscation", &state.obfuscateOep);
                option_pair("Anti-disassembly", &state.antiDisasm, "Mixed Boolean Arithmetic", &state.mba);
                option_pair("Encrypt sections", &state.encryptSections, "Generate fake instructions", &state.fakeInstructions);

                ImGui::TableNextRow(ImGuiTableRowFlags_None, option_row_h);
                ImGui::TableNextColumn();
                VCenterInRow(option_row_h);
                if (AnimatedCheckbox("Encrypt function", &state.packFunctions)) {
                    if (!state.packFunctions) {
                        state.fpackStart.fill(0);
                        state.fpackEnd.fill(0);
                    }
                }
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            if (state.packFunctions) {
                BeginPanelContent();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 0.0f));
                if (ImGui::BeginTable(
                        "FpackFields",
                        4,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX,
                        ImVec2(content_w, 0.0f))) {
                    ImGui::TableSetupColumn("start_label", ImGuiTableColumnFlags_WidthFixed, 34.0f);
                    ImGui::TableSetupColumn("start_field", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("end_label", ImGuiTableColumnFlags_WidthFixed, 26.0f);
                    ImGui::TableSetupColumn("end_field", ImGuiTableColumnFlags_WidthStretch);

                    ImGui::TableNextRow(0, option_row_h);
                    ImGui::TableNextColumn();
                    VCenterInRow(option_row_h);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("Start");
                    ImGui::TableNextColumn();
                    VCenterInRow(option_row_h);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputText("##fpack_start", state.fpackStart.data(), state.fpackStart.size());
                    ImGui::TableNextColumn();
                    VCenterInRow(option_row_h);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("End");
                    ImGui::TableNextColumn();
                    VCenterInRow(option_row_h);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputText("##fpack_end", state.fpackEnd.data(), state.fpackEnd.size());

                    ImGui::EndTable();
                }
                ImGui::PopStyleVar(2);
            }

        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("FileInfoColumn", ImVec2(rightColumnWidth, middleSectionHeight), false, kPanelChildFlags)) {
            const float panel_w = ImGui::GetWindowSize().x;
            const ImVec2 col_min = ImGui::GetWindowPos();
            const ImVec2 col_max(col_min.x + panel_w, col_min.y + ImGui::GetWindowSize().y);
            DrawPanelBorder(ImGui::GetWindowDrawList(), col_min, col_max, 12.0f);
            DrawPanelTitle("File Information", col_min, panel_w);
            BeginPanelContent();

            const float info_w = PanelContentWidth();
            const float info_row_h = ImGui::GetFrameHeight();

            if (state.fileInfo.isValid) {
                RenderFileInfoList(state.fileInfo, info_w, info_row_h);
            } else {
                const char* noFileText = "No file loaded";
                const float text_w = ImGui::CalcTextSize(noFileText).x;
                ImGui::SetCursorPosX(kPanelPad + (info_w - text_w) * 0.5f);
                ImGui::TextDisabled("%s", noFileText);
            }

        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // logs section
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float availHeight = avail.y;
        ImVec2 max = ImVec2(min.x + avail.x, min.y + availHeight);
        ImVec4 headerColor = kClrPanel;
        const float logsRounding = 12.0f;
        drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(headerColor), logsRounding);
        // panel border
        drawList->AddRect(
            min,
            max,
            ImGui::ColorConvertFloat4ToU32(kClrBorderSubtle),
            logsRounding,
            0,
            1.0f);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        const char* activityHeader = "Activity";
        const float activityTextWidth = ImGui::CalcTextSize(activityHeader).x;
        const float activityTextX = min.x + (avail.x - activityTextWidth) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(activityTextX, min.y + 10.0f));
        ImGui::TextDisabled("%s", activityHeader);
        
        const float logPadX = 12.0f;
        const float childStartY = min.y + 30.0f;
        const float childHeight = availHeight - (childStartY - min.y) - 10.0f;
        const float childWidth = avail.x - logPadX * 2.0f;
        
        ImGui::SetCursorScreenPos(ImVec2(min.x + logPadX, childStartY));

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
		// scrollbar styling
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 18.0f);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, kClrScrollbarGrab);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, kClrScrollbarGrabHovered);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(kClrAccent.x, kClrAccent.y, kClrAccent.z, 0.85f));
        if (ImGui::BeginChild("log_region", ImVec2(childWidth, childHeight), false, ImGuiWindowFlags_NoBackground)) {
            if (state.logEntries.empty()) {
                ImGui::TextDisabled("Logs will appear here.");
            } else {
                static size_t lastLogCount = 0;
                bool shouldAutoScroll = (state.logEntries.size() > lastLogCount);
                lastLogCount = state.logEntries.size();
                
                // set text wrap position to prevent overflow
                float wrapPos = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f;
                ImGui::PushTextWrapPos(wrapPos);
                
                ImGui::PushID(state.logAnimationGeneration);
                for (size_t logIndex = 0; logIndex < state.logEntries.size(); ++logIndex) {
                    const auto& entry = state.logEntries[logIndex];
                    ImGui::PushID(static_cast<int>(logIndex));
                    ImGuiID logAnimId = ImGui::GetID("log_anim");
                    float animProgress = ImGui::GetStateStorage()->GetFloat(logAnimId, 0.0f);
                    const float animStep = (std::min)(1.0f, ImGui::GetIO().DeltaTime * 9.0f);
                    animProgress = animProgress + (1.0f - animProgress) * animStep;
                    ImGui::GetStateStorage()->SetFloat(logAnimId, animProgress);

                    float yOffset = (1.0f - animProgress) * 10.0f;
                    if (yOffset > 0.1f) {
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yOffset);
                    }

                    ImVec4 color = entry.second ? kClrLogOk : kClrLogErr;
                    color.w *= animProgress;
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::TextWrapped("%s", entry.first.c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                ImGui::PopID();
                
                ImGui::PopTextWrapPos();
                
                if (shouldAutoScroll) {
                    ImGui::SetScrollY(ImGui::GetScrollMaxY());
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(3);
    }
    ImGui::End();
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext);

    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createDeviceFlags,
            featureLevelArray,
            2,
            D3D11_SDK_VERSION,
            &sd,
            &g_pSwapChain,
            &g_pd3dDevice,
            &featureLevel,
            &g_pd3dDeviceContext);
    }

    if (FAILED(res)) {
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (g_pSwapChain && SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) {
        POINT cursor{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &cursor);
        RECT rc{};
        GetClientRect(hWnd, &rc);

        // big cock
        const int kTitleBarHeight = 32;
        const int kIconSize = 22;
        const int kIconSpacing = 10;
        const int kRightPadding = 6;
        const int kExtraPad = 6;
        const int iconsRegionWidth = (kIconSize * 2) + kIconSpacing + kRightPadding + kExtraPad;
        if (cursor.y >= rc.top && cursor.y < rc.top + kTitleBarHeight &&
            cursor.x >= rc.right - iconsRegionWidth) {
            return HTCLIENT;
        }

        if (cursor.y >= rc.top && cursor.y < rc.top + 32) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_DROPFILES: {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        
        UINT fileCount = DragQueryFile(hDrop, 0xFFFFFFFF, nullptr, 0);
        
        if (fileCount > 0) {
            char filePath[MAX_PATH];
            if (DragQueryFileA(hDrop, 0, filePath, MAX_PATH) > 0) {
                POINT pt;
                DragQueryPoint(hDrop, &pt);
                
                POINT screenPt = pt;
                ClientToScreen(hWnd, &screenPt);
                
                g_droppedFilePath = filePath;
                
                ImVec2 dropPos(static_cast<float>(screenPt.x), static_cast<float>(screenPt.y));
                
                bool inInputField = (dropPos.x >= g_inputFieldMin.x && dropPos.x <= g_inputFieldMax.x &&
                                     dropPos.y >= g_inputFieldMin.y && dropPos.y <= g_inputFieldMax.y);
                bool inOutputField = (dropPos.x >= g_outputFieldMin.x && dropPos.x <= g_outputFieldMax.x &&
                                      dropPos.y >= g_outputFieldMin.y && dropPos.y <= g_outputFieldMax.y);
                
                if (inInputField) {
                    g_dropToInput = true;
                    g_hasDroppedFile = true;
                } else if (inOutputField) {
                    g_dropToInput = false;
                    g_hasDroppedFile = true;
                } else {
                    // fallback: use Y position if field bounds are not yet set :D
                    RECT rc;
                    GetClientRect(hWnd, &rc);
                    int windowHeight = rc.bottom - rc.top;
                    g_dropToInput = (pt.y < windowHeight / 2);
                    g_hasDroppedFile = true;
                }
            }
        }
        
        DragFinish(hDrop);
        return 0;
    }
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_DPICHANGED:
        if (const RECT* const prcNewWindow = reinterpret_cast<RECT*>(lParam)) {
            SetWindowPos(hWnd, nullptr,
                prcNewWindow->left,
                prcNewWindow->top,
                prcNewWindow->right - prcNewWindow->left,
                prcNewWindow->bottom - prcNewWindow->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

std::string BuildDefaultOutputPath(const std::string& input) {
    if (input.empty()) {
        return {};
    }

    std::filesystem::path inputPath(input);
    auto parent = inputPath.parent_path();
    std::string stem = inputPath.stem().string();
    if (stem.empty()) {
        stem = "packed";
    }

    std::filesystem::path outName = stem + "_packed.exe";
    if (!parent.empty()) {
        outName = parent / outName;
    }
    return outName.string();
}

static std::wstring ToLowerW(std::wstring s) {
    for (wchar_t& ch : s) {
        ch = std::towlower(ch);
    }
    return s;
}

static bool IsPathInCurrentDir(const std::filesystem::path& p) {
    std::error_code ec;
    const std::filesystem::path currentDir = std::filesystem::current_path(ec);
    if (ec) {
        return false;
    }

    const std::filesystem::path parent = p.parent_path();
    if (parent.empty()) {
        return false;
    }

    std::filesystem::path parentNorm = std::filesystem::weakly_canonical(parent, ec);
    if (ec) {
        parentNorm = parent.lexically_normal();
    }
    std::filesystem::path currentNorm = std::filesystem::weakly_canonical(currentDir, ec);
    if (ec) {
        currentNorm = currentDir.lexically_normal();
    }

    return ToLowerW(parentNorm.wstring()) == ToLowerW(currentNorm.wstring());
}

static std::string ShortenPathForDisplayIfInCurrentDir(const std::string& value) {
    if (value.empty()) {
        return value;
    }
    std::filesystem::path p(value);
    if (!p.has_filename()) {
        return value;
    }
    // before focus, show only filename
    return p.filename().string();
}

void CopyStringToBuffer(const std::string& value, std::array<char, MAX_PATH>& buffer) {
	// store full path in the main buffer
    strncpy_s(buffer.data(), buffer.size(), value.c_str(), buffer.size() - 1);
}

static void CopyShortStringToBuffer(const std::string& value, std::array<char, MAX_PATH>& buffer) {
    const std::string displayValue = ShortenPathForDisplayIfInCurrentDir(value);
    strncpy_s(buffer.data(), buffer.size(), displayValue.c_str(), buffer.size() - 1);
}

void SetDefaultOutputFromInput(GuiState& state) {
    if (state.outputCustomized) {
        return;
    }

    std::string defaultPath = BuildDefaultOutputPath(state.input.data());
    if (!defaultPath.empty()) {
        CopyStringToBuffer(defaultPath, state.output);
    }
}

static void ExtractFileInfo(GuiState& state) {
    state.fileInfo.isValid = false;
    state.fileInfo.architecture.clear();
    state.fileInfo.entryPoint.clear();
    state.fileInfo.imageBase.clear();
    state.fileInfo.imageSize.clear();
    state.fileInfo.sectionCount.clear();
    state.fileInfo.filePath.clear();
    state.fileInfo.aslrEnabled = false;

    std::string inputPath(state.input.data());
    if (inputPath.empty()) {
        return;
    }

    const pe_raw::ParseFileResult parsed = pe_raw::parse_pe_file(inputPath);
    if (!parsed.ok()) {
        state.fileInfo.isValid = false;
        return;
    }

    try {
        const pe_raw::PeView& view = *parsed.view;

        state.fileInfo.architecture = view.is64 ? "x64" : "x86";

        {
            std::ostringstream epStream;
            epStream << "0x" << std::hex << std::uppercase << view.entry_point_rva;
            state.fileInfo.entryPoint = epStream.str();
        }

        {
            std::ostringstream baseStream;
            baseStream << "0x" << std::hex << std::uppercase << view.image_base;
            state.fileInfo.imageBase = baseStream.str();
        }

        {
            std::ostringstream sizeStream;
            sizeStream << "0x" << std::hex << std::uppercase << view.size_of_image
                       << " (" << std::dec << view.size_of_image << " bytes)";
            state.fileInfo.imageSize = sizeStream.str();
        }

        state.fileInfo.sectionCount = std::to_string(view.num_sections);

        std::filesystem::path filePath(inputPath);
        state.fileInfo.filePath = filePath.filename().string();

        state.fileInfo.aslrEnabled =
            (view.dll_characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) != 0;

        state.fileInfo.isValid = true;
    }
    catch (...) {
        state.fileInfo.isValid = false;
    }
}

bool BrowseInputFile(GuiState& state) {
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetForegroundWindow();
    ofn.lpstrFile = state.input.data();
    ofn.nMaxFile = static_cast<DWORD>(state.input.size());
    ofn.lpstrFilter = "Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        state.outputCustomized = false;
        SetDefaultOutputFromInput(state);
        ExtractFileInfo(state);
        return true;
    }
    return false;
}

bool BrowseOutputFile(GuiState& state) {
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetForegroundWindow();
    ofn.lpstrFile = state.output.data();
    ofn.nMaxFile = static_cast<DWORD>(state.output.size());
    ofn.lpstrFilter = "Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0\0";
    ofn.lpstrDefExt = "exe";
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameA(&ofn) == TRUE) {
        state.outputCustomized = true;
        return true;
    }
    return false;
}

void ApplyBorderlessWindow(HWND hwnd, int width, int height) {
    if (!hwnd) {
        return;
    }

    RECT rect{};
    GetWindowRect(hwnd, &rect);

    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    style |= WS_POPUP;
    SetWindowLong(hwnd, GWL_STYLE, style);

    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    exStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(
        hwnd,
        nullptr,
        rect.left,
        rect.top,
        width,
        height,
        SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);

    const int radius = 24;
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius);
    if (region) {
        SetWindowRgn(hwnd, region, TRUE);
    }

#if defined(DWMWA_WINDOW_CORNER_PREFERENCE)
    const DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
#endif
}

// Some styles xD
void ApplyCustomTheme(ImGuiStyle& style) {
    style.WindowPadding = ImVec2(16.0f, 12.0f);
    style.FramePadding = ImVec2(9.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.WindowRounding = 16.0f;
    style.FrameRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 8.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;

    ImVec4 bg = kClrBg;
    ImVec4 panel = kClrPanel;
    ImVec4 accent = kClrAccent;
    ImVec4 accentHover = kClrAccentHover;
    ImVec4 accentActive = kClrAccentActive;

    style.Colors[ImGuiCol_WindowBg] = bg;
    style.Colors[ImGuiCol_ChildBg] = panel;
    style.Colors[ImGuiCol_PopupBg] = panel;
    
    ImVec4 sliderTrack = kClrSurface;
    style.Colors[ImGuiCol_FrameBg] = sliderTrack;
    style.Colors[ImGuiCol_FrameBgHovered] = kClrSurfaceHi;
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(kClrSurfaceHi.x + 0.02f, kClrSurfaceHi.y + 0.02f, kClrSurfaceHi.z + 0.02f, 1.0f);

    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = accentHover;

    style.Colors[ImGuiCol_Button] = kClrSurface;
    style.Colors[ImGuiCol_ButtonHovered] = kClrSurfaceHi;
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(kClrSurfaceHi.x + 0.02f, kClrSurfaceHi.y + 0.02f, kClrSurfaceHi.z + 0.02f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.25f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.38f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
    style.Colors[ImGuiCol_CheckMark] = kClrCheckMark;
    style.Colors[ImGuiCol_Separator] = kClrSeparator;
    style.Colors[ImGuiCol_ScrollbarGrab] = kClrScrollbarGrab;
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = kClrScrollbarGrabHovered;
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(kClrAccent.x, kClrAccent.y, kClrAccent.z, 0.85f);
    style.Colors[ImGuiCol_Text] = kClrText;
    style.Colors[ImGuiCol_TextDisabled] = kClrTextDim;
    style.Colors[ImGuiCol_Border] = kClrBorderSubtle;
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
}

void LoadUiFonts(ImGuiIO& io) {
    char win_dir[MAX_PATH]{};
    if (GetWindowsDirectoryA(win_dir, MAX_PATH) == 0) {
        io.Fonts->AddFontDefault();
        return;
    }

    const std::filesystem::path font_path = std::filesystem::path(win_dir) / "Fonts" / "segoeui.ttf";
    std::error_code ec;
    if (!std::filesystem::exists(font_path, ec)) {
        io.Fonts->AddFontDefault();
        return;
    }

    static const ImWchar ranges[] = {
        0x0020, 0x00FF,
        0x0400, 0x052F,
        0,
    };

    if (io.Fonts->AddFontFromFileTTF(font_path.string().c_str(), kUIFontSize, nullptr, ranges) == nullptr) {
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = kUIFontSize / 13.0f;
    }
}