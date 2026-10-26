#include "Player.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Runtime.h"
#include "lumen/core/Strings.h"

#include <algorithm>

using namespace wi::scene;

namespace lumen
{
	std::string FindPackage(const std::string& argument)
	{
		if (!argument.empty() && fs::IsFile(argument))
			return fs::Absolute(argument);
		const std::string dir = platform::ExecutableDir();
		for (const std::string& p : {fs::Join(dir, "data.lpak"), fs::Join(dir, "../data.lpak")})
			if (fs::IsFile(p))
				return fs::Normalize(p);
		return {};
	}

	PlayerApp::~PlayerApp()
	{
		host_.Stop();
	}

	Status PlayerApp::Prepare(const std::string& packagePath)
	{
		if (packagePath.empty())
			return Status::Fail("data.lpak was not found next to the game executable");
		Status s = pak_.Open(packagePath);
		if (!s)
			return Status::Fail("cannot open " + packagePath + ": " + s.message);
		// Cache key: size + modification time of the package, so every new build extracts into a fresh folder.
		char key[24];
		snprintf(key, sizeof(key), "%016llx", (unsigned long long)str::Hash64(std::to_string(fs::FileSize(packagePath)) + ":" + std::to_string(fs::LastWriteTimeMs(packagePath))));
		std::string base = fs::Join(fs::Parent(packagePath), ".lumen-data");
		if (!fs::IsWritableDirectory(fs::Parent(packagePath)))
			base = fs::Join(platform::UserDataDir(), "LumenGames", fs::Stem(packagePath));
		cacheDir_ = fs::Join(base, key);
		fs::CreateDirs(cacheDir_);
		std::string lum;
		s = ExtractGamePackage(pak_, cacheDir_, lum);
		if (!s)
			return s;
		// Older caches of the same game are removed (best effort).
		for (const fs::DirEntry& e : fs::ListDir(base))
			if (e.isDirectory && fs::FileName(e.path) != std::string(key))
				fs::RemoveAll(e.path);
		s = Project::Load(lum, project_);
		if (!s)
			return s;
		project_.Settings().scripts.hotReload = false;
		project_.Settings().native.hotReload = false;

		RuntimeOptions ro;
		ro.explicitRoot = fs::Join(cacheDir_, "runtime");
		ro.logName = fs::Stem(packagePath);
		s = Runtime::Initialize(ro);
		if (!s)
			return s;
		wi::backlog::SetLogFile(fs::Join(Runtime::Layout().logs, "wicked.log"));
		return SceneDoc::Load(project_.Resolve(project_.Settings().startupScene), scene_);
	}

	void PlayerApp::Initialize()
	{
		Application::Initialize();
		infoDisplay.active = false;
		infoDisplay.watermark = false;
		infoDisplay.fpsinfo = false;
		infoDisplay.resolution = false;
		infoDisplay.heap_allocation_counter = false;
		path_.init(canvas);
		path_.camera = &fallbackCam_;
		path_.Start();
		ActivatePath(&path_);
		wi::physics::SetEnabled(true);
		wi::physics::SetSimulationEnabled(true);
		wi::backlog::Lock();
		ApplyGraphics();
		if (project_.Settings().window.fullscreen)
			SetFullScreen(true);
		StartGame();
	}

	void PlayerApp::ApplyGraphics()
	{
		const auto& g = project_.Settings().graphics;
		wi::eventhandler::SetVSync(g.vsync);
		path_.setFXAAEnabled(g.antiAliasing == "fxaa");
		wi::renderer::SetTemporalAAEnabled(g.antiAliasing == "taa");
		path_.setMSAASampleCount((uint32_t)std::max(1, g.msaa));
		path_.setReflectionsEnabled(g.reflections);
		path_.setTonemap(g.tonemap == "reinhard" ? wi::renderer::Tonemap::Reinhard : g.tonemap == "uchimura" ? wi::renderer::Tonemap::Uchimura : wi::renderer::Tonemap::ACES);
		path_.setExposure(g.exposure);
		path_.setSaturation(g.saturation);
		path_.setContrast(g.contrast);
		path_.setBloomEnabled(g.bloom);
		path_.setBloomThreshold(g.bloomThreshold);
		path_.setDitherEnabled(g.dither);
		path_.setEyeAdaptionEnabled(g.eyeAdaptation);
		wi::renderer::SetOcclusionCullingEnabled(g.culling && g.occlusionCulling);
		wi::renderer::SetShadowsEnabled(g.shadows);
		wi::renderer::SetMeshletOcclusionCullingEnabled(g.culling && g.meshletOcclusionCulling);
		path_.resolutionScale = std::clamp(g.resolutionScale, 0.25f, 2.0f);
		wi::physics::SetAccuracy(project_.Settings().physics.accuracy);
		wi::physics::SetDebugDrawEnabled(false);
	}

	bool PlayerApp::LoadScene(const std::string& relativePath)
	{
		SceneDoc d;
		if (!SceneDoc::Load(project_.Resolve(relativePath), d))
			return false;
		pendingScene_ = relativePath; // applied at the start of the next update, never in the middle of a script call
		scene_ = std::move(d);
		return true;
	}

	void PlayerApp::StartGame()
	{
		host_.Stop();
		bridge_.Build(GetScene(), scene_, project_.Root());
		PlayContext ctx;
		ctx.project = &project_;
		ctx.doc = &scene_;
		ctx.bridge = &bridge_;
		ctx.scene = &GetScene();
		ctx.debugText = [](float, float, const std::string&) {};
		ctx.quit = [this](const std::string&) { quitRequested = true; };
		ctx.viewportCenterX = canvas.GetPhysicalWidth() * 0.5f;
		ctx.viewportCenterY = canvas.GetPhysicalHeight() * 0.5f;
		ctx.loadScene = [this](const std::string& path) { return LoadScene(path); };
		const Status s = host_.Start(ctx);
		if (!s)
			LUMEN_ERROR("game", "%s", s.message.c_str());
		started_ = true;
	}

	void PlayerApp::Update(float dt)
	{
		if (!pendingScene_.empty())
		{
			pendingScene_.clear();
			StartGame(); // scene_ already holds the new document; scripts of the new scene start fresh
		}
		const float w = canvas.GetLogicalWidth() * canvas.GetDPIScaling(), h = canvas.GetLogicalHeight() * canvas.GetDPIScaling();
		Scene& sc = GetScene();
		if (bridge_.MainCamera() != wi::ecs::INVALID_ENTITY)
		{
			if (CameraComponent* c = sc.cameras.GetComponent(bridge_.MainCamera()))
			{
				const TransformComponent* t = sc.transforms.GetComponent(bridge_.MainCamera());
				c->CreatePerspective(std::max(1.0f, w), std::max(1.0f, h), c->zNearP, c->zFarP, c->fov);
				if (t) c->TransformCamera(*t);
				c->UpdateCamera();
				path_.camera = c;
			}
		}
		else
		{
			fallbackCam_.CreatePerspective(std::max(1.0f, w), std::max(1.0f, h), 0.1f, 1000.0f);
			fallbackCam_.UpdateCamera();
			path_.camera = &fallbackCam_;
		}
		host_.SetScreen(canvas.GetLogicalWidth(), canvas.GetLogicalHeight(), 0, 0, canvas.GetDPIScaling());
		if (started_)
			host_.Update(dt);
		Application::Update(dt);
	}

	void PlayerApp::FixedUpdate()
	{
		Application::FixedUpdate();
		if (started_)
			host_.FixedUpdate(1.0f / targetFrameRate);
	}

	void PlayerApp::Render()
	{
		Application::Render();
	}

	void PlayerApp::Compose(wi::graphics::CommandList cmd)
	{
		Application::Compose(cmd);
		DrawUi(cmd);
	}

	// lumen.ui commands of this frame, drawn on top of the composed image (logical canvas units).
	void PlayerApp::DrawUi(wi::graphics::CommandList cmd)
	{
		for (const UiCmd& c : host_.UiCommands())
		{
			const XMFLOAT4 col(c.color[0], c.color[1], c.color[2], c.color[3]);
			switch (c.kind)
			{
			case UiCmd::Kind::Rect:
			case UiCmd::Kind::Image:
			{
				wi::image::Params p;
				p.pos = XMFLOAT3(c.x, c.y, 0);
				p.siz = XMFLOAT2(c.w, c.h);
				p.color = col;
				p.blendFlag = wi::enums::BLENDMODE_ALPHA;
				const wi::graphics::Texture* tex = c.kind == UiCmd::Kind::Image ? host_.UiTexture(c.text) : nullptr;
				if (c.kind == UiCmd::Kind::Image && !tex) break;
				if (c.kind == UiCmd::Kind::Rect && c.rounding > 0)
				{
					p.enableCornerRounding();
					for (auto& r : p.corners_rounding) { r.radius = (int)c.rounding; r.segments = 8; }
				}
				if (c.kind == UiCmd::Kind::Rect && c.thickness > 0)
				{
					// outline: four thin bars
					const float t = c.thickness;
					const float r[4][4] = {{c.x, c.y, c.w, t}, {c.x, c.y + c.h - t, c.w, t}, {c.x, c.y, t, c.h}, {c.x + c.w - t, c.y, t, c.h}};
					for (const auto& b : r)
					{
						wi::image::Params q;
						q.pos = XMFLOAT3(b[0], b[1], 0); q.siz = XMFLOAT2(b[2], b[3]); q.color = col; q.blendFlag = wi::enums::BLENDMODE_ALPHA;
						wi::image::Draw(nullptr, q, cmd);
					}
					break;
				}
				wi::image::Draw(tex, p, cmd);
				break;
			}
			case UiCmd::Kind::Line:
			{
				// thin rotated quad
				const float dx = c.w - c.x, dy = c.h - c.y, len = sqrtf(dx * dx + dy * dy);
				if (len < 0.01f) break;
				wi::image::Params p;
				p.pos = XMFLOAT3(c.x, c.y, 0);
				p.siz = XMFLOAT2(len, c.thickness);
				p.pivot = XMFLOAT2(0, 0.5f);
				p.rotation = atan2f(dy, dx);
				p.color = col;
				p.blendFlag = wi::enums::BLENDMODE_ALPHA;
				wi::image::Draw(nullptr, p, cmd);
				break;
			}
			case UiCmd::Kind::Text:
			{
				wi::font::Params fp(c.x, c.y, (int)c.size, c.align == 1 ? wi::font::WIFALIGN_CENTER : (c.align == 2 ? wi::font::WIFALIGN_RIGHT : wi::font::WIFALIGN_LEFT),
					wi::font::WIFALIGN_TOP, wi::Color::fromFloat4(col));
				if (c.shadow)
				{
					fp.shadowColor = wi::Color(0, 0, 0, (uint8_t)(c.color[3] * 180));
					fp.shadow_offset_x = 1;
					fp.shadow_offset_y = 1;
				}
				wi::font::Draw(c.text, fp, cmd);
				break;
			}
			}
		}
	}
}
