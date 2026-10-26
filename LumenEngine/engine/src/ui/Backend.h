// Lumen Engine — Dear ImGui <-> Wicked Engine renderer/platform glue.
#pragma once
#include "WickedEngine.h"
#include "imgui.h"
#include <string>

namespace lumen::ui
{
	struct BackendConfig
	{
		std::string shaderDir; // directory holding ImGuiVS.hlsl / ImGuiPS.hlsl
		std::string fontDir;   // directory holding Roboto-Medium.ttf and MaterialIcons-Regular.ttf
		std::string iniFile;   // layout persistence
	};

	bool Init(wi::platform::window_type window, const BackendConfig& config);
	void Shutdown();
	void NewFrame();
	void Draw(wi::graphics::CommandList cmd); // ImGui::Render() + record draw commands
	void ProcessEvent(const void* platformEvent); // SDL_Event* on SDL builds
	inline ImTextureID TextureId(const wi::graphics::Texture* t) { return (ImTextureID)t; }
	ImFont* BoldFont();
	ImFont* MonoFont();
	void ApplyTheme();
	namespace col
	{
		ImVec4 Accent();
		ImVec4 Muted();
		ImVec4 Good();
		ImVec4 Warn();
		ImVec4 Bad();
	}
}
