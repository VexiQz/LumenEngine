// Lumen Engine — the standalone game runtime (`lumen-player`): runs a released project without the editor.
#pragma once
#include "Bridge.h"
#include "Play.h"
#include "WickedEngine.h"
#include "lumen/core/GameBuilder.h"
#include "lumen/core/Project.h"
#include "lumen/core/SceneDoc.h"

namespace lumen
{
	class PlayerApp : public wi::Application
	{
	public:
		~PlayerApp() override;
		// Opens data.lpak (next to the executable or the path given), unpacks it into the cache and loads the manifest.
		// Must succeed before the window is created: it provides title, size and fullscreen.
		Status Prepare(const std::string& packagePath);
		void Initialize() override;
		void Update(float dt) override;
		void FixedUpdate() override;
		void Render() override;
		void Compose(wi::graphics::CommandList cmd) override;

		const Project& GetProject() const { return project_; }
		bool quitRequested = false;

	private:
		void ApplyGraphics();
		void DrawUi(wi::graphics::CommandList cmd);
		bool LoadScene(const std::string& relativePath);
		void StartGame();

		PakReader pak_;
		Project project_;
		SceneDoc scene_;
		Bridge bridge_;
		PlayHost host_;
		wi::RenderPath3D path_;
		wi::scene::CameraComponent fallbackCam_;
		bool started_ = false;
		std::string pendingScene_;
		std::string cacheDir_;
	};

	// Shared by both entry points. Returns the path of data.lpak to open (argument, next to the exe, or ""), and the log setup.
	std::string FindPackage(const std::string& argument);
}
