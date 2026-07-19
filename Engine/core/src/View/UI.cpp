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

    ImGui::BeginChild("First", ImVec2(300, 200));
    ImGui::Text(mStates.gpuAnimationEnabled.first);
    ImGui::Text(mStates.placeHolder1.first);
    ImGui::Text(mStates.placeHolder2.first);
    ImGui::Separator();
    ImGui::Text("Screen Resolution");
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("Second", ImVec2(200, 200));
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
        }
        ImGui::EndCombo();
    }

    ImGui::EndChild();
    ImGui::End();
    ImGui::Render();

    return mStates;
}
