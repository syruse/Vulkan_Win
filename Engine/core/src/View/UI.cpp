#include "UI.h"
#include <imgui/imgui.h>

void UI::draw() const {
    // ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    // ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(15, 15));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImGui::SetWindowSize(ImVec2(0, 0));
    ImGui::SetWindowFontScale(1.3);
    // ImGui::SetCursorPos(ImVec2(0, 0));

    ImGui::BeginChild("First", ImVec2(150, 150));
    ImGui::Button("AAA");
    ImGui::Button("AAA");
    ImGui::Button("AAA");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Second", ImVec2(300, 150));
    ImGui::Button("AAA");
    ImGui::Button("AAA");
    ImGui::Button("AAA");
    ImGui::EndChild();
    ImGui::Text("This is some useful text.");
    ImGui::Text("This is some useful text.");
    ImGui::Text("This is some useful text.");
    ImGui::End();
    ImGui::Render();
}
