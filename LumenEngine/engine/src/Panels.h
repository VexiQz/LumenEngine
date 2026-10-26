// Lumen Engine — shared state handed to every editor panel.
#pragma once
#include "Editor.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Strings.h"
#include "ui/Backend.h"
#include "IconsMaterialDesign.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"

namespace lumen
{
	// Render path that draws only into its own render target; the viewport panel shows that texture.
	class ViewportPath : public wi::RenderPath3D
	{
	public:
		void Compose(wi::graphics::CommandList) const override {}
	};

	struct EditorCamera
	{
		XMFLOAT3 position{0, 4, -9};
		float yaw = 0, pitch = 0.35f; // radians
		float distance = 9;           // orbit distance
		XMFLOAT3 pivot{0, 1, 0};
		float speed = 8.0f;
		float fov = 60;
		void Apply(wi::scene::CameraComponent& cam, float w, float h) const;
	};

	struct Ui
	{
		Editor* ed = nullptr;
		wi::scene::Scene* wscene = nullptr;
		Bridge* bridge = nullptr;
		ViewportPath* vp = nullptr;
		PlayHost* host = nullptr;
		wi::scene::CameraComponent* editCam = nullptr;
		EditorCamera* cam = nullptr;
		// Filled by the viewport panel every frame:
		float vpX = 0, vpY = 0, vpW = 1280, vpH = 720; // screen space rect of the rendered image (logical px)
		bool vpHovered = false, vpFocused = false;
		float dpi = 1.0f;
		std::string pendingScript; // set by the inspector to open a file
		const std::vector<std::pair<std::pair<float, float>, std::string>>* debugTexts = nullptr;
		bool graphicsDirty = false;
		bool prefsChanged = true; // apply on the next frame
		bool showAbout = false;
		bool showNewProject = false;
		bool showCommandPalette = false;
	};

	void DrawHub(Ui& ui);               // project picker / creator (when no project is open)
	void DrawMainMenu(Ui& ui);
	void DrawToolbar(Ui& ui);
	void DrawStatusBar(Ui& ui);
	void DrawViewport(Ui& ui);
	void DrawHierarchy(Ui& ui);
	void DrawInspector(Ui& ui);
	void DrawEnvironment(Ui& ui);
	void DrawAssets(Ui& ui);
	void DrawConsole(Ui& ui);
	void DrawCodeEditor(Ui& ui);
	void TerrainSculpt(Ui& ui, float originX, float originY, float availW, float availH, bool hovered); // viewport brush
	void DrawDebugger(Ui& ui);          // Lua debugger: stack, variables, watch, breakpoints
	void DrawTimeline(Ui& ui);          // .lanim keyframe clips
	void DrawBuild(Ui& ui);             // toolchain + native module + export
	void DrawPackages(Ui& ui);
	void DrawLibraries(Ui& ui);         // native render/audio libraries
	void DrawShaders(Ui& ui);           // built-in shader library switches
	void DrawProjectSettings(Ui& ui);
	void DrawPreferences(Ui& ui);
	void DrawToasts(Ui& ui);
	void DrawCommandPalette(Ui& ui);

	// small widgets
	bool IconButton(const char* icon, const char* tooltip, bool active = false, ImVec2 size = ImVec2(0, 0));
	void SectionLabel(const char* text);
	bool Vec3Control(const char* label, Float3& v, float speed = 0.05f, float resetTo = 0, const char* fmt = "%.3f");
	bool ColorEdit3(const char* label, Float3& c);
	bool PathField(const char* label, std::string& path, const char* filter, Ui& ui);
	const char* IconForAsset(AssetType t);
	std::string FormatBytes(uint64_t b);
}
