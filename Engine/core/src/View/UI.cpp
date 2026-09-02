#include "UI.h"
#include <imgui/imgui.h>

const UI::States& UI::updateAndDraw() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (mShowMenu) {
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 28.0f, viewport->WorkPos.y + 28.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(520.0f, 480.0f), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.06f, 0.075f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.27f, 0.54f, 0.57f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.08f, 0.28f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.10f, 0.38f, 0.40f, 1.0f));

    ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.95f, 0.91f, 1.0f));
    ImGui::TextUnformatted("Settings");
    ImGui::PopStyleColor();
    ImGui::Separator();

    const auto drawToggle = [](const char* id, bool& enabled) {
        ImGui::PushID(id);
        ImGui::PushStyleColor(ImGuiCol_Button, enabled ? ImVec4(0.08f, 0.42f, 0.37f, 1.0f) : ImVec4(0.18f, 0.20f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, enabled ? ImVec4(0.11f, 0.54f, 0.47f, 1.0f) : ImVec4(0.25f, 0.28f, 0.30f, 1.0f));
        const bool pressed = ImGui::Button(enabled ? "ON" : "OFF", ImVec2(62.0f, 0.0f));
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        return pressed;
    };

    ImGui::TextDisabled("PERFORMANCE");
    ImGui::TextUnformatted("GPU animation");
    ImGui::SameLine(300.0f);
    if (drawToggle("gpuAnimation", mStates.gpuAnimationEnabled.second)) {
        mStates.gpuAnimationEnabled.second = !mStates.gpuAnimationEnabled.second;
    }
    ImGui::TextUnformatted("Option slot 1");
    ImGui::SameLine(300.0f);
    if (drawToggle("optionOne", mStates.placeHolder1.second)) {
        mStates.placeHolder1.second = !mStates.placeHolder1.second;
    }
    ImGui::TextUnformatted("Option slot 2");
    ImGui::SameLine(300.0f);
    if (drawToggle("optionTwo", mStates.placeHolder2.second)) {
        mStates.placeHolder2.second = !mStates.placeHolder2.second;
    }

    ImGui::Separator();
    ImGui::TextDisabled("DISPLAY");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Resolution");
    ImGui::SameLine(130.0f);
    mStates.resolutionChanged = false;
    if (ImGui::BeginCombo("##resolution", m_resolutions[m_selectedIdx].label, ImGuiComboFlags_WidthFitPreview)) {
        for (int i = 0; i < static_cast<int>(m_resolutions.size()); i++) {
            const bool isSelected = (m_selectedIdx == i);
            if (ImGui::Selectable(m_resolutions[i].label, isSelected)) {
                if (!isSelected) {
                    m_selectedIdx = i;
                    mStates.nextWidth = m_resolutions[i].width;
                    mStates.nextHeight = m_resolutions[i].height;
                    mStates.resolutionChanged = true;
                }
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    ImGui::TextDisabled("UPSCALE");
    mStates.upscalerChanged = false;
    {
        int idx = static_cast<int>(mStates.upscalerType);
        bool changed = false;
        if (ImGui::RadioButton("Native", &idx, 0)) changed = true;
        ImGui::SameLine();
        if (!mDlssSupported) ImGui::BeginDisabled();
        if (ImGui::RadioButton("DLSS", &idx, 1)) changed = true;
        if (!mDlssSupported) ImGui::EndDisabled();
        ImGui::SameLine();
        if (!mXessSupported) ImGui::BeginDisabled();
        if (ImGui::RadioButton("XeSS", &idx, 2)) changed = true;
        if (!mXessSupported) ImGui::EndDisabled();
        if (changed) {
            mStates.upscalerType   = static_cast<UpscalerType>(idx);
            mStates.upscalerChanged = true;
        }
    }
    {
        const bool hasUpscaler = (mStates.upscalerType != UpscalerType::None);
        if (!hasUpscaler) ImGui::BeginDisabled();
        static const char* kPresetLabels[] = {
            "Native AA", "Ultra Quality", "Quality", "Balanced", "Performance", "Ultra Performance"
        };
        int presetIdx = static_cast<int>(mStates.upscalerPreset);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Quality preset");
        ImGui::SameLine(130.0f);
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::Combo("##preset", &presetIdx, kPresetLabels, 6)) {
            mStates.upscalerPreset  = static_cast<UpscalerPreset>(presetIdx);
            if (hasUpscaler) mStates.upscalerChanged = true;
        }
        if (!hasUpscaler) ImGui::EndDisabled();
    }

    ImGui::Separator();
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - ImGui::GetFrameHeight());
    if (ImGui::Button("Exit", ImVec2(-1.0f, 0.0f))) {
        mStates.exitRequested = true;
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(4);
    }

    if (mIsLoading) {
        // Keep this compact overlay visible while the scene has not loaded yet.
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                       viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36.0f, 24.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.06f, 0.08f, 0.94f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.27f, 0.54f, 0.57f, 0.90f));
        ImGui::Begin("##LoadingOverlay", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::SetWindowFontScale(2.25f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.98f, 0.93f, 1.0f));
        ImGui::TextUnformatted("Loading...");
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    ImGui::Render();

    return mStates;
}
