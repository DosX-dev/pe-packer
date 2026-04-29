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

#include "../core/core.hpp"
#include "../handler/handler.hpp"
#include "../utils/arguments.hpp"
#include "../utils/utils.hpp"
#include "../pe_lib/pe_bliss.h"
#include "../pe_lib/pe_factory.h"

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

struct FileInfo {
    std::string architecture;
    std::string entryPoint;
    std::string imageBase;
    std::string imageSize;
    std::string sectionCount;
    std::string filePath;
    bool isValid = false;
};

struct GuiState {
    std::array<char, MAX_PATH> input{};
    std::array<char, MAX_PATH> output{};
    // display buffers: show only filename until the user focuses the field
    std::array<char, MAX_PATH> inputDisplay{};
    std::array<char, MAX_PATH> outputDisplay{};
    std::array<char, 32> fpackStart{};
    std::array<char, 32> fpackEnd{};
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
static bool AnimatedIntSlider(const char* label, int* value, int minValue, int maxValue);
static void SetDefaultOutputFromInput(GuiState& state);
static void CopyStringToBuffer(const std::string& value, std::array<char, MAX_PATH>& buffer);
static void CopyShortStringToBuffer(const std::string& value, std::array<char, MAX_PATH>& buffer);
static void ApplyBorderlessWindow(HWND hwnd, int width, int height);
static void ApplyCustomTheme(ImGuiStyle& style);
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
        const auto mutations = static_cast<uint32_t>(state.mutationBase * 10);
        print_info("Mutations count: %u\n", mutations);
        auto packer = std::make_unique<c_core>(inputPath, outputPath, mutations);
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
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * 0.72f, style.FramePadding.y * 0.62f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
    const bool changed = ImGui::Checkbox(label, value);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGuiID itemId = ImGui::GetItemID();
    if (itemId == 0) {
        return changed;
    }

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID animId = itemId ^ 0x51A7E9B1u;

    float progress = storage->GetFloat(animId, *value ? 1.0f : 0.0f);
    const float target = *value ? 1.0f : 0.0f;
    const float lerpFactor = (std::min)(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
    progress = progress + (target - progress) * lerpFactor;
    storage->SetFloat(animId, progress);

    if (progress <= 0.001f) {
        return changed;
    }

    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const float boxSize = itemMax.y - itemMin.y;
    const ImVec2 boxMin = itemMin;
    const ImVec2 boxMax(itemMin.x + boxSize, itemMin.y + boxSize);
    const float pad = (std::max)(2.0f, boxSize * 0.18f);
    const float thickness = (std::max)(1.6f, boxSize * 0.10f);

    const ImVec2 p1(boxMin.x + boxSize * 0.25f, boxMin.y + boxSize * 0.54f);
    const ImVec2 p2(boxMin.x + boxSize * 0.45f, boxMax.y - pad);
    const ImVec2 p3(boxMax.x - pad, boxMin.y + pad);

    const ImVec2 center((boxMin.x + boxMax.x) * 0.5f, (boxMin.y + boxMax.y) * 0.5f);
    const ImVec2 a(center.x + (p1.x - center.x) * progress, center.y + (p1.y - center.y) * progress);
    const ImVec2 b(center.x + (p2.x - center.x) * progress, center.y + (p2.y - center.y) * progress);
    const ImVec2 c(center.x + (p3.x - center.x) * progress, center.y + (p3.y - center.y) * progress);

    ImVec4 checkColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    checkColor.w *= progress;
    const ImU32 color = ImGui::GetColorU32(checkColor);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddLine(a, b, color, thickness);
    drawList->AddLine(b, c, color, thickness);
    return changed;
}

bool AnimatedIntSlider(const char* label, int* value, int minValue, int maxValue) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);

    char valueText[32];
    std::snprintf(valueText, sizeof(valueText), "%d%%", *value);
    const ImVec2 valueTextSize = ImGui::CalcTextSize(valueText);

    // reserve space on the right so the % doesn't overlap the slider track
    const float gapRight = 10.0f;
    float availWidth = ImGui::GetContentRegionAvail().x;
    float sliderWidth = availWidth - valueTextSize.x - gapRight;
    if (sliderWidth < 60.0f) {
        sliderWidth = availWidth; // fallback: not enough room, keep it inside
    }
    ImGui::SetNextItemWidth(sliderWidth);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool changed = ImGui::SliderInt("##animated_slider", value, minValue, maxValue, "%d");
    ImGui::PopStyleColor(5);

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
    draw->AddRectFilled(trackMin, trackMax, ImGui::GetColorU32(ImVec4(0.14f, 0.16f, 0.20f, 1.0f)), rounding);
    draw->AddRectFilled(trackMin, ImVec2(knobX, trackMax.y), ImGui::GetColorU32(ImVec4(0.32f, 0.61f, 0.93f, 0.95f)), rounding);

    const ImU32 knobColor = ImGui::GetColorU32(ImGui::IsItemActive() ? ImVec4(0.92f, 0.97f, 1.0f, 1.0f) : ImVec4(0.82f, 0.90f, 0.98f, 1.0f));
    draw->AddCircleFilled(ImVec2(knobX, centerY), knobRadius, knobColor, 24);
    draw->AddCircle(ImVec2(knobX, centerY), knobRadius, ImGui::GetColorU32(ImVec4(0.12f, 0.18f, 0.28f, 0.65f)), 24, 1.0f);

    // draw percent for slider
    const float textX = (sliderWidth <= availWidth - valueTextSize.x - gapRight + 0.01f)
        ? (frameMax.x + gapRight * 0.5f)
        : (frameMax.x - valueTextSize.x - 8.0f);
    const float textY = centerY - valueTextSize.y * 0.5f;
    draw->AddText(ImVec2(textX, textY), ImGui::GetColorU32(ImVec4(0.86f, 0.91f, 0.97f, 0.95f)), valueText);

    ImGui::PopID();
    return changed;
}

void RenderGui(GuiState& state, std::string& statusMessage, bool& lastSuccess) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("pe-packer GUI", nullptr, windowFlags)) {
        // title bar
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.88f, 0.95f, 1.0f));
        ImGui::Text("pe-packer");
        const ImVec2 titleTextMin = ImGui::GetItemRectMin();
        const ImVec2 titleTextMax = ImGui::GetItemRectMax();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("| Version 1.0.3");
        const ImVec2 versionTextMin = ImGui::GetItemRectMin();
        const ImVec2 versionTextMax = ImGui::GetItemRectMax();
        
        // title bar icons
        const float iconSize = 22.0f;
        const float iconSpacing = 10.0f;
        const float rightPadding = 6.0f;
        float windowRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
        float btnRowWidth = iconSize * 2.0f + iconSpacing;
        const float lineMinY = (std::min)(titleTextMin.y, versionTextMin.y);
        const float lineMaxY = (std::max)(titleTextMax.y, versionTextMax.y);
        const float lineH = lineMaxY - lineMinY;
        const float iconsY = lineMinY + (lineH - iconSize) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(windowRight - rightPadding - btnRowWidth, iconsY));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.29f, 0.47f, 0.83f, 0.20f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.40f, 0.75f, 0.30f));
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
            ImVec4 col = hov || act ? ImVec4(0.45f, 0.62f, 0.95f, 1.0f) : ImVec4(0.82f, 0.88f, 0.95f, 1.0f);
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
            ImVec4 col = hov || act ? ImVec4(0.91f, 0.48f, 0.40f, 1.0f) : ImVec4(0.82f, 0.88f, 0.95f, 1.0f);
            ImU32 colorU32 = ImGui::GetColorU32(col);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddLine(ImVec2(bmin.x + pad, bmin.y + pad), ImVec2(bmax.x - pad, bmax.y - pad), colorU32, 2.0f);
            dl->AddLine(ImVec2(bmin.x + pad, bmax.y - pad), ImVec2(bmax.x - pad, bmin.y + pad), colorU32, 2.0f);
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        
        ImGui::Spacing();

        const float browseWidth = 110.0f;
        const float minFieldWidth = 160.0f;
        const float pathLabelWidth = (std::max)(ImGui::CalcTextSize("Input file").x, ImGui::CalcTextSize("Output file").x);
        ImGuiStyle& style = ImGui::GetStyle();

        auto fileInputRow = [&](const char* label,
                                 std::array<char, MAX_PATH>& fullBuffer,
                                 std::array<char, MAX_PATH>& displayBuffer,
                                 auto&& browseFn,
                                 bool isInput) -> bool {
            ImGui::PushID(label);
            const float rowStartX = ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            ImGui::SetCursorPosX(rowStartX + pathLabelWidth + style.ItemInnerSpacing.x);
            float avail = ImGui::GetContentRegionAvail().x;
            float fieldWidth = avail - browseWidth - style.ItemSpacing.x;
            if (fieldWidth < minFieldWidth) {
                fieldWidth = minFieldWidth;
            }
            ImGui::SetNextItemWidth(fieldWidth);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, style.FramePadding.y * 0.62f));
            
            ImVec2 fieldMin = ImGui::GetCursorScreenPos();
            const float fieldHeight = ImGui::GetFrameHeight();
            ImVec2 fieldMaxGuess(fieldMin.x + fieldWidth, fieldMin.y + fieldHeight);

            bool showFull = isInput ? state.inputShowFullPath : state.outputShowFullPath;
            // if user clicked inside the field this frame we gonna show full path
            if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(fieldMin, fieldMaxGuess, true)) {
                showFull = true;
            }

            if (!showFull) {
                CopyShortStringToBuffer(std::string(fullBuffer.data()), displayBuffer);
            }

            bool editedFromInput = false;

            // apply drag&drop before drawing the input
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
                editedFromInput = true;
                g_hasDroppedFile = false;
            }

            const ImGuiInputTextFlags inputFlags = showFull ? 0 : ImGuiInputTextFlags_ReadOnly;
            char* inputPtr = showFull ? fullBuffer.data() : displayBuffer.data();
            editedFromInput = ImGui::InputText("##path", inputPtr, static_cast<int>(fullBuffer.size()), inputFlags) || editedFromInput;

            const float inputHeight = ImGui::GetItemRectSize().y;

            if (isInput) {
                state.inputShowFullPath = ImGui::IsItemActive();
            } else {
                state.outputShowFullPath = ImGui::IsItemActive();
            }
            ImGui::PopStyleVar();
            
            // store field bounds
            ImVec2 fieldMax = ImGui::GetItemRectMax();
            if (isInput) {
                g_inputFieldMin = fieldMin;
                g_inputFieldMax = fieldMax;
            } else {
                g_outputFieldMin = fieldMin;
                g_outputFieldMax = fieldMax;
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE")) {
                }
                ImGui::EndDragDropTarget();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("##browse_btn", ImVec2(browseWidth, inputHeight)) && browseFn()) {
                editedFromInput = true;
            }
            const ImVec2 btnMin = ImGui::GetItemRectMin();
            const ImVec2 btnMax = ImGui::GetItemRectMax();
            const ImVec2 btnSize(btnMax.x - btnMin.x, btnMax.y - btnMin.y);
            const char* browseText = "Browse";
            const ImVec2 textSize = ImGui::CalcTextSize(browseText);
            const ImVec2 textPos(
                btnMin.x + (btnSize.x - textSize.x) * 0.5f,
                btnMin.y + (btnSize.y - textSize.y) * 0.5f - 1.0f
            );
            ImGui::GetWindowDrawList()->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), browseText);
            ImGui::PopID();
            return editedFromInput;
        };

        if (fileInputRow("Input file", state.input, state.inputDisplay, [&]() { return BrowseInputFile(state); }, true)) {
            SetDefaultOutputFromInput(state);
            ExtractFileInfo(state);
        }

        if (fileInputRow("Output file", state.output, state.outputDisplay, [&]() { return BrowseOutputFile(state); }, false)) {
            state.outputCustomized = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // reserve space for logs at the bottom
        float logsHeight = 200.0f;
        float middleSectionHeight = ImGui::GetContentRegionAvail().y - logsHeight - style.ItemSpacing.y;
        if (middleSectionHeight < 100.0f) {
            middleSectionHeight = 100.0f;
        }

        float leftColumnWidth = ImGui::GetContentRegionAvail().x * 0.55f;
        float rightColumnWidth = ImGui::GetContentRegionAvail().x * 0.45f - style.ItemSpacing.x;
        
        if (ImGui::BeginChild("OptionsColumn", ImVec2(leftColumnWidth, middleSectionHeight), false)) {
			// rounded border
            const float panelRounding = 16.0f;
            const ImVec4 borderCol = ImVec4(0.29f, 0.47f, 0.83f, 0.32f);
            ImDrawList* colDraw = ImGui::GetWindowDrawList();
            ImVec2 colMin = ImGui::GetWindowPos();
            ImVec2 colMax = ImVec2(colMin.x + ImGui::GetWindowSize().x, colMin.y + ImGui::GetWindowSize().y);
            colDraw->AddRect(colMin, colMax, ImGui::ColorConvertFloat4ToU32(borderCol), panelRounding, 0, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.88f, 0.95f, 1.0f));
            const char* optionsTitle = "Options";
            float optionsTextWidth = ImGui::CalcTextSize(optionsTitle).x;
            float optionsAvailableWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (optionsAvailableWidth - optionsTextWidth) * 0.5f);
            const ImVec2 titleScreenPos = ImGui::GetCursorScreenPos();
            const ImVec2 titleSize = ImGui::CalcTextSize(optionsTitle);
            const float textShiftY = -6.0f;
            const ImVec2 shiftedTitlePos(titleScreenPos.x, titleScreenPos.y + textShiftY);
            const ImU32 childBg = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
            const float coverPadX = 14.0f;
            const float coverTop = shiftedTitlePos.y - 2.0f;
            const float coverBottom = shiftedTitlePos.y + titleSize.y + 4.0f;
            colDraw->AddRectFilled(
                ImVec2(shiftedTitlePos.x - coverPadX, coverTop),
                ImVec2(shiftedTitlePos.x + titleSize.x + coverPadX, coverBottom),
                childBg,
                0.0f);
            ImGui::GetForegroundDrawList()->AddText(
                shiftedTitlePos,
                ImGui::GetColorU32(ImVec4(0.82f, 0.88f, 0.95f, 1.0f)),
                optionsTitle);
            ImGui::Dummy(ImVec2(0.0f, titleSize.y));
            ImGui::PopStyleColor();
            
            ImGui::Spacing();
            ImGui::Indent(4.0f);
            
            AnimatedIntSlider("Mutation Cycles", &state.mutationBase, 1, 100);
            //ImGui::SameLine();
            //ImGui::TextDisabled("Mutations count");
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.f, 4.f));
            if (ImGui::BeginTable("OptionsFlags", 2, ImGuiTableFlags_SizingStretchProp)) {
                auto checkbox = [](const char* label, bool* value) {
                    ImGui::TableNextColumn();
                    AnimatedCheckbox(label, value);
                };
                checkbox("Remove ASLR", &state.removeAslr);
                checkbox("OEP call obfuscation", &state.obfuscateOep);
                checkbox("Anti-disassembly", &state.antiDisasm);
                checkbox("Mixed Boolean Arithmetic", &state.mba);
                checkbox("Encrypt sections", &state.encryptSections);
                checkbox("Generate fake instructions", &state.fakeInstructions);
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            ImGui::Spacing();
            if (AnimatedCheckbox("Encrypt function", &state.packFunctions)) {
                if (!state.packFunctions) {
                    state.fpackStart.fill(0);
                    state.fpackEnd.fill(0);
                }
            }

            if (state.packFunctions) {
                ImGui::Indent();
                ImGui::InputText("Start address", state.fpackStart.data(), state.fpackStart.size());
                ImGui::InputText("End address", state.fpackEnd.data(), state.fpackEnd.size());
                ImGui::Unindent();
            }
            ImGui::Unindent(4.0f);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("FileInfoColumn", ImVec2(rightColumnWidth, middleSectionHeight), false)) {
            // rounded border
            const float panelRounding = 16.0f;
            const ImVec4 borderCol = ImVec4(0.29f, 0.47f, 0.83f, 0.32f);
            ImDrawList* colDraw = ImGui::GetWindowDrawList();
            ImVec2 colMin = ImGui::GetWindowPos();
            ImVec2 colMax = ImVec2(colMin.x + ImGui::GetWindowSize().x, colMin.y + ImGui::GetWindowSize().y);
            colDraw->AddRect(colMin, colMax, ImGui::ColorConvertFloat4ToU32(borderCol), panelRounding, 0, 1.0f);

            // header
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.88f, 0.95f, 1.0f));
            const char* fileInfoTitle = "File Information";
            float fileInfoTextWidth = ImGui::CalcTextSize(fileInfoTitle).x;
            float fileInfoAvailableWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (fileInfoAvailableWidth - fileInfoTextWidth) * 0.5f);
            const ImVec2 titleScreenPos = ImGui::GetCursorScreenPos();
            const ImVec2 titleSize = ImGui::CalcTextSize(fileInfoTitle);
            const float textShiftY = -6.0f;
            const ImVec2 shiftedTitlePos(titleScreenPos.x, titleScreenPos.y + textShiftY);
            const ImU32 childBg = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
            const float coverPadX = 14.0f;
            const float coverTop = shiftedTitlePos.y - 2.0f;
            const float coverBottom = shiftedTitlePos.y + titleSize.y + 4.0f;
            colDraw->AddRectFilled(
                ImVec2(shiftedTitlePos.x - coverPadX, coverTop),
                ImVec2(shiftedTitlePos.x + titleSize.x + coverPadX, coverBottom),
                childBg,
                0.0f);
            ImGui::GetForegroundDrawList()->AddText(
                shiftedTitlePos,
                ImGui::GetColorU32(ImVec4(0.82f, 0.88f, 0.95f, 1.0f)),
                fileInfoTitle);
            ImGui::Dummy(ImVec2(0.0f, titleSize.y));
            ImGui::PopStyleColor();

            if (state.fileInfo.isValid) {
                
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.f, 3.f));
                if (ImGui::BeginTable("FileInfo", 2, ImGuiTableFlags_SizingStretchProp)) {
                    auto centerInColumn = [](const char* text) {
                        // center the next text within the current table column
                        const float colW = ImGui::GetColumnWidth();
                        const float textW = ImGui::CalcTextSize(text).x;
                        const float cursorX = ImGui::GetCursorPosX();
                        ImGui::SetCursorPosX(cursorX + (colW - textW) * 0.5f);
                    };

                    // left column
                    ImGui::TableNextColumn();
                    centerInColumn("File:");
                    ImGui::TextDisabled("File:");
                    centerInColumn(state.fileInfo.filePath.c_str());
                    ImGui::Text("%s", state.fileInfo.filePath.c_str());
                    
                    centerInColumn("Architecture:");
                    ImGui::TextDisabled("Architecture:");
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.82f, 0.53f, 1.0f));
                    centerInColumn(state.fileInfo.architecture.c_str());
                    ImGui::Text("%s", state.fileInfo.architecture.c_str());
                    ImGui::PopStyleColor();
                    
                    centerInColumn("Entry Point:");
                    ImGui::TextDisabled("Entry Point:");
                    centerInColumn(state.fileInfo.entryPoint.c_str());
                    ImGui::Text("%s", state.fileInfo.entryPoint.c_str());

                    // right column
                    ImGui::TableNextColumn();
                    centerInColumn("Image Base:");
                    ImGui::TextDisabled("Image Base:");
                    centerInColumn(state.fileInfo.imageBase.c_str());
                    ImGui::Text("%s", state.fileInfo.imageBase.c_str());
                    
                    centerInColumn("Image Size:");
                    ImGui::TextDisabled("Image Size:");
                    centerInColumn(state.fileInfo.imageSize.c_str());
                    ImGui::Text("%s", state.fileInfo.imageSize.c_str());
                    
                    centerInColumn("Sections:");
                    ImGui::TextDisabled("Sections:");
                    centerInColumn(state.fileInfo.sectionCount.c_str());
                    ImGui::Text("%s", state.fileInfo.sectionCount.c_str());

                    ImGui::EndTable();
                }
                ImGui::PopStyleVar();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
                const char* noFileText = "No file loaded";
                float textWidth = ImGui::CalcTextSize(noFileText).x;
                float availWidth = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - textWidth) * 0.5f);
                ImGui::TextDisabled("%s", noFileText);
                ImGui::PopStyleColor();
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // actions block
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.88f, 0.95f, 1.0f));
            float actionsTextWidth = ImGui::CalcTextSize("Actions").x;
            float actionsAvailableWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (actionsAvailableWidth - actionsTextWidth) * 0.5f);
            ImGui::Text("Actions");
            ImGui::PopStyleColor();
            
            ImGui::Spacing();
            
            const float buttonWidth = 120.0f;
            float availableWidth = ImGui::GetContentRegionAvail().x;
            float totalButtonsWidth = buttonWidth;
            float offset = (availableWidth - totalButtonsWidth) * 0.5f;
            if (offset < 0.0f) {
                offset = 0.0f;
            }
            float startX = ImGui::GetCursorPosX() + offset;
            ImGui::SetCursorPosX(startX);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5.0f);
            
            if (ImGui::Button("Pack", ImVec2(buttonWidth, 0.0f))) {
                lastSuccess = ExecutePacking(state, statusMessage);
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // logs section
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float availHeight = avail.y;
        ImVec2 max = ImVec2(min.x + avail.x, min.y + availHeight);
        ImVec4 headerColor = ImVec4(0.16f, 0.18f, 0.22f, 0.95f);
        const float logsRounding = 16.0f;
        drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(headerColor), logsRounding);
        // panel border
        drawList->AddRect(
            min,
            max,
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.29f, 0.47f, 0.83f, 0.32f)),
            logsRounding,
            0,
            1.0f);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        const char* activityHeader = "Activity";
        float activityTextWidth = ImGui::CalcTextSize(activityHeader).x;
        float activityTextX = min.x + (avail.x - activityTextWidth) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(activityTextX, min.y + 8.0f));
        ImGui::TextDisabled("%s", activityHeader);
        
        // calculate proper size for child window
        float childStartY = min.y + 28.0f;
        float childHeight = availHeight - (childStartY - min.y) - 8.0f; // 8px padding at bottom
        float childWidth = avail.x - 16.0f; // 8px padding on each side
        
        ImGui::SetCursorScreenPos(ImVec2(min.x + 8.0f, childStartY));

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
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

                    ImVec4 color = entry.second ? ImVec4(0.45f, 0.82f, 0.53f, 1.0f) : ImVec4(0.91f, 0.48f, 0.40f, 1.0f);
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
        ImGui::PopStyleVar();
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
    // before focus, show only filename (no full path)
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

    std::string inputPath(state.input.data());
    if (inputPath.empty()) {
        return;
    }

    try {
        std::ifstream pe_file(inputPath, std::ios::in | std::ios::binary);
        if (!pe_file) {
            return;
        }

        auto peImage = std::make_unique<pe_bliss::pe_base>(pe_bliss::pe_factory::create_pe(pe_file));
        pe_bliss::pe_type peType = peImage->get_pe_type();
        
        if (peType != pe_bliss::pe_type_32 && peType != pe_bliss::pe_type_64) {
            return;
        }

        // architecture
        if (peType == pe_bliss::pe_type_64) {
            state.fileInfo.architecture = "x64";
        } else {
            state.fileInfo.architecture = "x86";
        }

        // entry point
        uint32_t ep = peImage->get_ep();
        std::ostringstream epStream;
        epStream << "0x" << std::hex << std::uppercase << ep;
        state.fileInfo.entryPoint = epStream.str();

        // image base
        std::ostringstream baseStream;
        if (peType == pe_bliss::pe_type_64) {
            uint64_t base = peImage->get_image_base_64();
            baseStream << "0x" << std::hex << std::uppercase << base;
        } else {
            uint32_t base = peImage->get_image_base_32();
            baseStream << "0x" << std::hex << std::uppercase << base;
        }
        state.fileInfo.imageBase = baseStream.str();

        // image size
        uint32_t imageSize = peImage->get_size_of_image();
        std::ostringstream sizeStream;
        sizeStream << "0x" << std::hex << std::uppercase << imageSize 
                   << " (" << std::dec << imageSize << " bytes)";
        state.fileInfo.imageSize = sizeStream.str();

        // section count
        uint16_t sectionCount = peImage->get_number_of_sections();
        state.fileInfo.sectionCount = std::to_string(sectionCount);

        // file path
        std::filesystem::path filePath(inputPath);
        state.fileInfo.filePath = filePath.filename().string();

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
    style.WindowPadding = ImVec2(20.0f, 16.0f);
    style.FramePadding = ImVec2(12.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.WindowRounding = 20.0f;
    style.FrameRounding = 10.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 10.0f;
    style.GrabMinSize = 12.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;

    ImVec4 bg = ImVec4(0.11f, 0.13f, 0.17f, 1.0f);
    ImVec4 panel = ImVec4(0.16f, 0.18f, 0.24f, 1.0f);
    ImVec4 accent = ImVec4(0.29f, 0.47f, 0.83f, 1.0f);
    ImVec4 accentHover = ImVec4(0.36f, 0.55f, 0.91f, 1.0f);
    ImVec4 accentActive = ImVec4(0.24f, 0.40f, 0.75f, 1.0f);

    style.Colors[ImGuiCol_WindowBg] = bg;
    style.Colors[ImGuiCol_ChildBg] = ImVec4(bg.x + 0.01f, bg.y + 0.01f, bg.z + 0.02f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] = panel;
    
    ImVec4 sliderTrack = ImVec4(0.20f, 0.22f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = sliderTrack;
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.26f, 0.32f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.30f, 0.36f, 1.0f);

    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.62f, 0.95f, 1.0f);
    
    style.Colors[ImGuiCol_Button] = accent;
    style.Colors[ImGuiCol_ButtonHovered] = accentHover;
    style.Colors[ImGuiCol_ButtonActive] = accentActive;
    style.Colors[ImGuiCol_Header] = accent;
    style.Colors[ImGuiCol_HeaderHovered] = accentHover;
    style.Colors[ImGuiCol_HeaderActive] = accentActive;
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.92f, 0.95f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.27f, 0.32f, 1.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.94f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.64f, 0.70f, 1.0f);
}