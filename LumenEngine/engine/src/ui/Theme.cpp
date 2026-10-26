#include "ui/Backend.h"

namespace lumen::ui
{
	namespace col
	{
		ImVec4 Accent() { return ImVec4(0.33f, 0.62f, 1.0f, 1.0f); } // cool blue like the Wicked editor: the only saturated colour of the UI
		ImVec4 Muted() { return ImVec4(0.52f, 0.54f, 0.58f, 1.0f); }
		ImVec4 Good() { return ImVec4(0.45f, 0.78f, 0.52f, 1.0f); }
		ImVec4 Warn() { return ImVec4(0.93f, 0.78f, 0.36f, 1.0f); }
		ImVec4 Bad() { return ImVec4(0.92f, 0.42f, 0.40f, 1.0f); }
	}

	void ApplyTheme()
	{
		ImGuiStyle& s = ImGui::GetStyle();
		s.WindowRounding = 5;
		s.ChildRounding = 3;
		s.FrameRounding = 4;
		s.PopupRounding = 4;
		s.ScrollbarRounding = 6;
		s.GrabRounding = 3;
		s.TabRounding = 4;
		s.WindowBorderSize = 1;
		s.FrameBorderSize = 0;
		s.PopupBorderSize = 1;
		s.WindowPadding = ImVec2(10, 8);
		s.FramePadding = ImVec2(8, 4);
		s.ItemSpacing = ImVec2(8, 5);
		s.ItemInnerSpacing = ImVec2(6, 4);
		s.IndentSpacing = 16;
		s.ScrollbarSize = 11;
		s.GrabMinSize = 9;
		s.WindowMenuButtonPosition = ImGuiDir_None;

		const ImVec4 acc = col::Accent();
		auto C = [&](ImGuiCol i, float r, float g, float b, float a = 1) { s.Colors[i] = ImVec4(r, g, b, a); };
		auto A = [&](ImGuiCol i, float a) { s.Colors[i] = ImVec4(acc.x, acc.y, acc.z, a); };
		C(ImGuiCol_Text, 0.86f, 0.87f, 0.89f);
		C(ImGuiCol_TextDisabled, 0.46f, 0.48f, 0.52f);
		C(ImGuiCol_WindowBg, 0.105f, 0.11f, 0.125f);
		C(ImGuiCol_ChildBg, 0.095f, 0.10f, 0.113f);
		C(ImGuiCol_PopupBg, 0.13f, 0.135f, 0.15f, 0.98f);
		C(ImGuiCol_Border, 0.19f, 0.20f, 0.225f);
		C(ImGuiCol_BorderShadow, 0, 0, 0, 0);
		C(ImGuiCol_FrameBg, 0.15f, 0.16f, 0.18f);
		C(ImGuiCol_FrameBgHovered, 0.19f, 0.20f, 0.23f);
		C(ImGuiCol_FrameBgActive, 0.22f, 0.23f, 0.26f);
		C(ImGuiCol_TitleBg, 0.085f, 0.09f, 0.10f);
		C(ImGuiCol_TitleBgActive, 0.085f, 0.09f, 0.10f);
		C(ImGuiCol_TitleBgCollapsed, 0.085f, 0.09f, 0.10f);
		C(ImGuiCol_MenuBarBg, 0.085f, 0.09f, 0.10f);
		C(ImGuiCol_ScrollbarBg, 0, 0, 0, 0);
		C(ImGuiCol_ScrollbarGrab, 0.24f, 0.25f, 0.28f);
		C(ImGuiCol_ScrollbarGrabHovered, 0.32f, 0.33f, 0.37f);
		C(ImGuiCol_ScrollbarGrabActive, 0.40f, 0.41f, 0.45f);
		A(ImGuiCol_CheckMark, 1.0f);
		A(ImGuiCol_SliderGrab, 0.85f);
		A(ImGuiCol_SliderGrabActive, 1.0f);
		C(ImGuiCol_Button, 0.17f, 0.18f, 0.205f);
		C(ImGuiCol_ButtonHovered, 0.23f, 0.24f, 0.27f);
		C(ImGuiCol_ButtonActive, 0.28f, 0.29f, 0.33f);
		C(ImGuiCol_Header, 0.17f, 0.18f, 0.205f);
		A(ImGuiCol_HeaderHovered, 0.22f);
		A(ImGuiCol_HeaderActive, 0.32f);
		C(ImGuiCol_Separator, 0.19f, 0.20f, 0.225f);
		A(ImGuiCol_SeparatorHovered, 0.6f);
		A(ImGuiCol_SeparatorActive, 1.0f);
		C(ImGuiCol_ResizeGrip, 0, 0, 0, 0);
		A(ImGuiCol_ResizeGripHovered, 0.5f);
		A(ImGuiCol_ResizeGripActive, 0.8f);
		C(ImGuiCol_Tab, 0.095f, 0.10f, 0.113f);
		C(ImGuiCol_TabHovered, 0.18f, 0.19f, 0.215f);
		A(ImGuiCol_TabActive, 0.28f);
		C(ImGuiCol_TabUnfocused, 0.095f, 0.10f, 0.113f);
		C(ImGuiCol_TabUnfocusedActive, 0.12f, 0.125f, 0.14f);
		A(ImGuiCol_DockingPreview, 0.35f);
		C(ImGuiCol_DockingEmptyBg, 0.07f, 0.075f, 0.085f);
		A(ImGuiCol_TextSelectedBg, 0.30f);
		A(ImGuiCol_DragDropTarget, 0.9f);
		A(ImGuiCol_NavHighlight, 0.7f);
		C(ImGuiCol_TableHeaderBg, 0.12f, 0.13f, 0.145f);
		C(ImGuiCol_TableBorderStrong, 0.19f, 0.20f, 0.225f);
		C(ImGuiCol_TableBorderLight, 0.15f, 0.16f, 0.18f);
		C(ImGuiCol_TableRowBg, 0, 0, 0, 0);
		C(ImGuiCol_TableRowBgAlt, 1, 1, 1, 0.02f);
		A(ImGuiCol_PlotHistogram, 0.9f);
	}
}
