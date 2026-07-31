#include "UI.h"
#include <imgui/imgui.h>

const UI::States& UI::updateAndDraw() {
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin(
        "Menu", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::SetWindowSize(ImVec2(0, 0));
    ImGui::SetWindowFontScale(1.15);

    const char* off = "OFF";
    const char* on = "ON";

    ImGui::BeginChild("First", ImVec2(300, 280));
    ImGui::Text(mStates.gpuAnimationEnabled.first);
    ImGui::Text(mStates.placeHolder1.first);
    ImGui::Text(mStates.placeHolder2.first);
    ImGui::Separator();
    ImGui::Text("Screen Resolution");
    ImGui::Separator();
    ImGui::Text("Upscaler");
    ImGui::Text("Preset");
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("Second", ImVec2(260, 280));
    ImGui::PushID(100);
    if (ImGui::Button(mStates.gpuAnimationEnabled.second ? on : off)) {
        mStates.gpuAnimationEnabled.second = !mStates.gpuAnimationEnabled.second;
    }
    ImGui::PopID();
    ImGui::PushID(101);
    if (ImGui::Button(mStates.placeHolder1.second ? on : off)) {
        mStates.placeHolder1.second = !mStates.placeHolder1.second;    
    }
    ImGui::PopID();
    ImGui::PushID(102);
    if (ImGui::Button(mStates.placeHolder2.second ? on : off)) {
        mStates.placeHolder2.second = !mStates.placeHolder2.second;    
    }
    ImGui::PopID();

    ImGui::Separator();
    mStates.resolutionChanged = false;
    if (ImGui::BeginCombo("##res", m_resolutions[m_selectedIdx].label)) {
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
    mStates.upscalerChanged = false;
    {
        int idx = static_cast<int>(mStates.upscalerType);
        bool changed = false;
        if (ImGui::RadioButton("None", &idx, 0)) changed = true;
        ImGui::SameLine();
        if (ImGui::RadioButton("DLSS", &idx, 1)) changed = true;
        ImGui::SameLine();
        if (ImGui::RadioButton("XeSS", &idx, 2)) changed = true;
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
        if (ImGui::Combo("##preset", &presetIdx, kPresetLabels, 6)) {
            mStates.upscalerPreset  = static_cast<UpscalerPreset>(presetIdx);
            if (hasUpscaler) mStates.upscalerChanged = true;
        }
        if (!hasUpscaler) ImGui::EndDisabled();
    }

    ImGui::EndChild();
    ImGui::End();
    ImGui::Render();

    return mStates;
}
