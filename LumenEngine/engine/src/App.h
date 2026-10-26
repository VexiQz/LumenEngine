// Lumen Engine — the editor application (wi::Application subclass).
#pragma once
#include "Panels.h"

namespace lumen
{
	class EditorApp : public wi::Application
	{
	public:
		~EditorApp() override;
		void Initialize() override;
		void Update(float dt) override;
		void FixedUpdate() override;
		void Render() override;
		void Compose(wi::graphics::CommandList cmd) override;

		std::function<void()> pumpEvents; // set by the platform entry point: drains OS/window events (used while the debugger holds the game)
		std::string bootProject; // optional project path from the command line
		bool quitRequested = false;

	private:
		void BuildLayout(unsigned int dockId);
		void ApplyGraphics();
		void StartPlay();
		void StopPlay();
		void ApplyBreakpoints(lua::Debugger& dbg);
		void HandleShortcuts(Ui& ui);
		void UpdateCamera(Ui& ui, float dt);
		void PickAt(Ui& ui, float x, float y, bool additive);

		std::unique_ptr<Editor> ed_;
		Bridge bridge_;
		ViewportPath vp_;
		PlayHost host_;
		NavSystem navPreview_; // edit mode navmesh preview (Navigation mesh inspector)
		wi::scene::CameraComponent editCam_;
		EditorCamera cam_;
		Ui ui_;
		bool layoutBuilt_ = false;
		uint32_t renderW_ = 1280, renderH_ = 720;
		bool graphicsDirty_ = true;
		std::string iniPath_;
		std::string loadedScenePath_;
		bool sceneInWicked_ = false;
		uint64_t syncedVersion_ = 0;
		float hotReloadTimer_ = 0;
		float extTimer_ = 0;
		float autosaveTimer_ = 0;
		std::vector<std::pair<std::pair<float, float>, std::string>> debugTexts_;
		bool diagnosticsFirstFrame_ = true;
	};
}
