#include "UI.h"
#include <imgui/imgui.h>

const UI::States& UI::updateAndDraw() {
    // ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    // ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(15, 15));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin(
        "Menu", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::SetWindowSize(ImVec2(0, 0));
    ImGui::SetWindowFontScale(1.15);
    //ImGui::SetCursorPos(ImVec2(0, 0));

    // since we use the same button name then the first button will react on clicking
    // that's why we need to use "OFF##1".."OFF##3" or set manually unique id by PushID
    const char* off = "OFF";
    const char* on = "ON";

    ImGui::BeginChild("First", ImVec2(300, 150));
    ImGui::Text(mStates.gpuAnimationEnabled.first.c_str());
    ImGui::Text(mStates.placeHolder1.first.c_str());
    ImGui::Text(mStates.placeHolder2.first.c_str());
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Second", ImVec2(150, 150));
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
    ImGui::EndChild();
    ImGui::End();
    ImGui::Render();

    return mStates;
}
