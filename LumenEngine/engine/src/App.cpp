#include "App.h"
#include "Diagnostics.h"
#include <chrono>
#include <thread>

#include "lumen/core/Fs.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <cmath>

using namespace wi::scene;

namespace lumen
{
	void EditorCamera::Apply(CameraComponent& c, float w, float h) const
	{
		c.CreatePerspective(w, h, 0.05f, 4000.0f, fov * 3.14159265f / 180.0f);
		TransformComponent t;
		t.translation_local = position;
		XMStoreFloat4(&t.rotation_local, XMQuaternionRotationRollPitchYaw(pitch, yaw, 0));
		t.UpdateTransform();
		c.TransformCamera(t);
		c.UpdateCamera();
	}

	EditorApp::~EditorApp()
	{
		host_.Stop();
		ui::Shutdown();
	}

	void EditorApp::Initialize()
	{
		diagnostics::Stage("Wicked Application::Initialize");
		Application::Initialize();
		infoDisplay.active = false;
		infoDisplay.watermark = false;
		infoDisplay.fpsinfo = false;
		infoDisplay.resolution = false;
		infoDisplay.heap_allocation_counter = false;

		const RuntimeLayout& rl = Runtime::Layout();
		ui::BackendConfig cfg;
		cfg.shaderDir = fs::Join(rl.shaders, "ui");
		cfg.fontDir = fs::Join(rl.root, "fonts");
		iniPath_ = fs::Join(rl.config, "editor_layout.ini");
		cfg.iniFile = iniPath_;
		diagnostics::Stage("ImGui backend initialization");
		if (!ui::Init(window, cfg))
			LUMEN_ERROR("ui", "UI backend initialisation failed (shaders: %s)", cfg.shaderDir.c_str());
		layoutBuilt_ = fs::Exists(iniPath_);

		diagnostics::Stage("Editor and viewport initialization");
		ed_ = std::make_unique<Editor>();
		vp_.init(renderW_, renderH_, 96);
		vp_.camera = &editCam_;
		vp_.Start();
		vp_.setFXAAEnabled(true);
		vp_.setOutlineEnabled(true);
		wi::physics::SetEnabled(true);
		wi::physics::SetSimulationEnabled(false);
		wi::backlog::Lock();
		if (!bootProject.empty())
		{
			diagnostics::Stage("Opening command-line project");
			Status s = ed_->OpenProject(bootProject);
			if (!s)
				ed_->Toast(s.message, true);
		}
		// Required native libraries (Linux: SDL2/Vulkan) are verified in the background so a missing one is reported, not fatal.
		if (ed_->nativeLibs)
			ed_->StartJob("Checking runtime libraries", [this](Job& j) {
				std::vector<std::string> report;
				Status s = ed_->nativeLibs->EnsureRequired({}, [&j](const std::string& step, uint64_t d, uint64_t t) {
					j.SetStatus(step);
					if (t) j.progress = float(d) / float(t);
				}, &report, &j.cancel);
				return s;
			});
		diagnostics::Stage("Editor initialization complete");
	}

	void EditorApp::ApplyGraphics()
	{
		graphicsDirty_ = false;
		if (!ed_->HasProject())
			return;
		const auto& g = ed_->project.Settings().graphics;
		wi::eventhandler::SetVSync(g.vsync);
		vp_.setFXAAEnabled(g.antiAliasing == "fxaa");
		wi::renderer::SetTemporalAAEnabled(g.antiAliasing == "taa");
		vp_.setMSAASampleCount((uint32_t)std::max(1, g.msaa));
		vp_.setReflectionsEnabled(g.reflections);
		vp_.setTonemap(g.tonemap == "reinhard" ? wi::renderer::Tonemap::Reinhard : g.tonemap == "uchimura" ? wi::renderer::Tonemap::Uchimura : wi::renderer::Tonemap::ACES);
		vp_.setExposure(g.exposure);
		vp_.setSaturation(g.saturation);
		vp_.setContrast(g.contrast);
		vp_.setBloomEnabled(g.bloom);
		vp_.setBloomThreshold(g.bloomThreshold);
		vp_.setDitherEnabled(g.dither);
		vp_.setEyeAdaptionEnabled(g.eyeAdaptation);
		wi::renderer::SetOcclusionCullingEnabled(g.culling && g.occlusionCulling);
		wi::renderer::SetShadowsEnabled(g.shadows);
		wi::renderer::SetMeshletOcclusionCullingEnabled(g.culling && g.meshletOcclusionCulling);
		wi::physics::SetAccuracy(ed_->project.Settings().physics.accuracy);
		wi::physics::SetDebugDrawEnabled(ed_->project.Settings().physics.debugDraw);
		vp_.ResizeBuffers();
	}

	void EditorApp::StartPlay()
	{
		if (!ed_->HasProject())
			return;
		ed_->SaveAllFiles();
		for (const CodeDoc& d : ed_->docs)
			if (d.lua && !d.diags.empty())
			{
				bool err = false;
				for (const LuaDiag& x : d.diags) err |= x.error;
				if (err)
				{
					ed_->Toast("Fix syntax errors in " + d.path + " before playing", true);
					ed_->play = PlayState::Edit;
					return;
				}
			}
		bridge_.Build(GetScene(), ed_->scene, ed_->project.Root());
		wi::physics::SetSimulationEnabled(true);
		PlayContext ctx;
		ctx.project = &ed_->project;
		ctx.doc = &ed_->scene;
		ctx.bridge = &bridge_;
		ctx.scene = &GetScene();
		ctx.debugText = [this](float x, float y, const std::string& t) { debugTexts_.push_back({{x, y}, t}); };
		ctx.quit = [this](const std::string&) { ed_->wantStop = true; };
		ctx.viewportCenterX = (ui_.vpX + ui_.vpW * 0.5f) * ui_.dpi;
		ctx.viewportCenterY = (ui_.vpY + ui_.vpH * 0.5f) * ui_.dpi;
		ctx.loadScene = [this](const std::string& path) {
			SceneDoc d;
			if (!SceneDoc::Load(ed_->project.Resolve(path), d))
				return false;
			ed_->scene = std::move(d);
			bridge_.Build(GetScene(), ed_->scene, ed_->project.Root());
			return true;
		};
		ctx.debuggerSetup = [this](lua::Debugger& dbg) {
			ed_->bpApplied = 0;
			ApplyBreakpoints(dbg);
			dbg.pump = [this] {
				if (pumpEvents) pumpEvents();
				Run(); // one full nested frame: UI stays interactive, scripts and physics stay frozen
			};
			dbg.onPause = [this](lua::Debugger& d) {
				Editor& e = *ed_;
				e.dbg.paused = true;
				e.dbg.file = d.CurrentFile();
				e.dbg.line = d.CurrentLine();
				e.dbg.reason = d.PauseReason();
				e.dbg.frame = 0;
				++e.dbg.pausedSerial;
				e.panel.debug = true;
				ImGui::SetWindowFocus(ICON_MD_BUG_REPORT "  Debugger");
				if (e.dbg.followPause)
				{
					const std::string root = fs::Normalize(e.project.Root());
					std::string rel = d.CurrentFile();
					if (rel.size() > root.size() && rel.compare(0, root.size(), root) == 0) rel = rel.substr(root.size() + 1);
					e.OpenFile(rel);
					if (e.activeDoc >= 0 && e.activeDoc < (int)e.docs.size()) e.docs[e.activeDoc].goToLine = d.CurrentLine();
					e.panel.code = true;
				}
				host_.SetCursorLocked(false);
			};
			dbg.onResume = [this] { ed_->dbg.paused = false; };
		};
		Status s = host_.Start(ctx);
		if (!s)
			ed_->Toast("Script error: " + s.message, true);
		ed_->play = PlayState::Play;
		ImGui::SetWindowFocus("Viewport");
	}

	void EditorApp::ApplyBreakpoints(lua::Debugger& dbg)
	{
		Editor& ed = *ed_;
		if (ed.bpApplied == ed.bpVersion)
			return;
		ed.bpApplied = ed.bpVersion;
		dbg.ClearBreakpoints();
		for (const auto& f : ed.breakpoints)
			for (const EdBreakpoint& b : f.second)
				if (b.enabled)
					dbg.SetBreakpoint(ed.AbsPath(f.first), b.line, b.cond);
	}

	void EditorApp::StopPlay()
	{
		host_.Stop();
		wi::physics::SetSimulationEnabled(false);
		ed_->play = PlayState::Edit;
		// restore the edit scene
		SceneDoc d;
		if (SceneDoc::Load(ed_->project.Resolve(ed_->scenePath), d) && !ed_->sceneDirty)
			ed_->scene = std::move(d);
		bridge_.Build(GetScene(), ed_->scene, ed_->project.Root());
		syncedVersion_ = ed_->version;
		debugTexts_.clear();
	}

	void EditorApp::HandleShortcuts(Ui& ui)
	{
		ImGuiIO& io = ImGui::GetIO();
		Editor& ed = *ed_;
		const bool typing = io.WantTextInput;
		const bool ctrl = io.KeyCtrl;
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		{
			ed.SaveAllFiles();
			if (ed.HasProject())
			{
				Status s = ed.SaveScene();
				ed.Toast(s ? "Saved" : s.message, !s);
			}
		}
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false))
			ui.showCommandPalette = true;
		if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
			(ed.play == PlayState::Edit ? ed.wantPlay : ed.wantStop) = true;
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ed.play != PlayState::Edit && !ImGui::GetIO().WantCaptureKeyboard && !host_.CursorLocked())
			ed.wantStop = true;
		if (typing || ed.play != PlayState::Edit)
			return;
		// numpad views (Ctrl = opposite side) around the orbit pivot, Home frames the whole scene
		{
			auto view = [&](float yaw, float pitch) {
				cam_.yaw = yaw;
				cam_.pitch = pitch;
				const XMVECTOR fwd = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), XMQuaternionRotationRollPitchYaw(pitch, yaw, 0));
				XMStoreFloat3(&cam_.position, XMVectorSubtract(XMLoadFloat3(&cam_.pivot), XMVectorScale(fwd, cam_.distance)));
			};
			const float pi = 3.14159265f, flip = io.KeyCtrl ? pi : 0.0f;
			if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) view(flip, 0);
			if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) view(-pi * 0.5f + flip, 0);
			if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) view(0, io.KeyCtrl ? -1.5f : 1.5f);
			if (ImGui::IsKeyPressed(ImGuiKey_Home, false) && !ed.scene.entities.empty())
			{
				wi::primitive::AABB all;
				bool any = false;
				for (const SceneEntity& e : ed.scene.entities)
				{
					wi::primitive::AABB bb;
					if (bridge_.Bounds(GetScene(), ed.scene, e.id, bb)) { all = any ? wi::primitive::AABB::Merge(all, bb) : bb; any = true; }
				}
				if (any)
				{
					const XMFLOAT3 c = all.getCenter(), h = all.getHalfWidth();
					cam_.pivot = c;
					cam_.distance = std::max(3.0f, sqrtf(h.x * h.x + h.y * h.y + h.z * h.z) * 2.4f);
					view(cam_.yaw, cam_.pitch);
				}
			}
		}
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, true)) ed.Undo();
		if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, true) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, true)))) ed.Redo();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) ed.DuplicateSelection();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) ed.CopySelection();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) ed.CutSelection();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) ed.Paste(io.KeyShift ? ed.primary : std::string());
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) ed.SelectAll();
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ed.selection.empty()) ed.Select("");
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) ed.DeleteSelection();
		if (!io.MouseDown[1])
		{
			if (ImGui::IsKeyPressed(ImGuiKey_W, false)) ed.gizmo = GizmoMode::Translate;
			if (ImGui::IsKeyPressed(ImGuiKey_E, false)) ed.gizmo = GizmoMode::Rotate;
			if (ImGui::IsKeyPressed(ImGuiKey_R, false)) ed.gizmo = GizmoMode::Scale;
			if (ImGui::IsKeyPressed(ImGuiKey_F, false)) ed.FocusSelection();
		}
	}

	void EditorApp::UpdateCamera(Ui& ui, float dt)
	{
		ImGuiIO& io = ImGui::GetIO();
		Editor& ed = *ed_;
		if (ed.play != PlayState::Edit)
			return;
		if (ed.focusRequest)
		{
			ed.focusRequest = false;
			wi::primitive::AABB bb;
			if (!ed.primary.empty() && bridge_.Bounds(GetScene(), ed.scene, ed.primary, bb))
			{
				const XMFLOAT3 c = bb.getCenter(), h = bb.getHalfWidth();
				cam_.pivot = c;
				cam_.distance = std::max(1.5f, sqrtf(h.x * h.x + h.y * h.y + h.z * h.z) * 2.6f);
			}
			else if (SceneEntity* e = ed.PrimaryEntity())
			{
				cam_.pivot = XMFLOAT3(e->transform.position.x, e->transform.position.y, e->transform.position.z);
				cam_.distance = 5;
			}
			XMVECTOR fwd = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), XMQuaternionRotationRollPitchYaw(cam_.pitch, cam_.yaw, 0));
			XMStoreFloat3(&cam_.position, XMVectorSubtract(XMLoadFloat3(&cam_.pivot), XMVectorScale(fwd, cam_.distance)));
		}
		if (!ui.vpHovered && !ui.vpFocused)
			return;
		const bool rmb = io.MouseDown[1], mmb = io.MouseDown[2], alt = io.KeyAlt;
		XMVECTOR fwd = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), XMQuaternionRotationRollPitchYaw(cam_.pitch, cam_.yaw, 0));
		XMVECTOR right = XMVector3Rotate(XMVectorSet(1, 0, 0, 0), XMQuaternionRotationRollPitchYaw(cam_.pitch, cam_.yaw, 0));
		XMVECTOR up = XMVector3Cross(fwd, right);
		XMVECTOR pos = XMLoadFloat3(&cam_.position);
		if (rmb && ui.vpHovered)
		{
			const float sens = ed.prefs.mouseSensitivity, inv = ed.prefs.invertY ? -1.0f : 1.0f;
			cam_.yaw += io.MouseDelta.x * 0.0035f * sens;
			cam_.pitch = std::max(-1.55f, std::min(1.55f, cam_.pitch + io.MouseDelta.y * 0.0035f * sens * inv));
			float sp = cam_.speed * (io.KeyShift ? 4.0f : 1.0f) * (io.KeyCtrl ? 0.25f : 1.0f);
			XMVECTOR move = XMVectorZero();
			if (ImGui::IsKeyDown(ImGuiKey_W)) move = XMVectorAdd(move, fwd);
			if (ImGui::IsKeyDown(ImGuiKey_S)) move = XMVectorSubtract(move, fwd);
			if (ImGui::IsKeyDown(ImGuiKey_D)) move = XMVectorAdd(move, right);
			if (ImGui::IsKeyDown(ImGuiKey_A)) move = XMVectorSubtract(move, right);
			if (ImGui::IsKeyDown(ImGuiKey_E)) move = XMVectorAdd(move, XMVectorSet(0, 1, 0, 0));
			if (ImGui::IsKeyDown(ImGuiKey_Q)) move = XMVectorSubtract(move, XMVectorSet(0, 1, 0, 0));
			pos = XMVectorAdd(pos, XMVectorScale(move, sp * dt));
			if (io.MouseWheel != 0) cam_.speed = std::max(0.5f, std::min(200.0f, cam_.speed * (1.0f + io.MouseWheel * 0.15f)));
		}
		else if (mmb || (alt && io.MouseDown[0]))
		{
			if (alt && !mmb)
			{
				// orbit around the pivot
				cam_.yaw += io.MouseDelta.x * 0.005f;
				cam_.pitch = std::max(-1.55f, std::min(1.55f, cam_.pitch + io.MouseDelta.y * 0.005f));
				fwd = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), XMQuaternionRotationRollPitchYaw(cam_.pitch, cam_.yaw, 0));
				pos = XMVectorSubtract(XMLoadFloat3(&cam_.pivot), XMVectorScale(fwd, cam_.distance));
			}
			else
			{
				const float k = cam_.distance * 0.0016f;
				XMVECTOR d = XMVectorAdd(XMVectorScale(right, -io.MouseDelta.x * k), XMVectorScale(up, io.MouseDelta.y * k));
				pos = XMVectorAdd(pos, d);
				XMStoreFloat3(&cam_.pivot, XMVectorAdd(XMLoadFloat3(&cam_.pivot), d));
			}
		}
		else if (io.MouseWheel != 0 && ui.vpHovered)
		{
			cam_.distance = std::max(0.3f, cam_.distance * (1.0f - io.MouseWheel * 0.12f));
			pos = XMVectorSubtract(XMLoadFloat3(&cam_.pivot), XMVectorScale(fwd, cam_.distance));
		}
		XMStoreFloat3(&cam_.position, pos);
		if (rmb)
		{
			// keep the orbit pivot in front of the camera so Alt+LMB and F behave after flying around
			XMStoreFloat3(&cam_.pivot, XMVectorAdd(pos, XMVectorScale(fwd, cam_.distance)));
		}
	}

	void EditorApp::PickAt(Ui& ui, float x, float y, bool additive)
	{
		const wi::primitive::Ray ray = wi::renderer::GetPickRay((long)x, (long)y, vp_, editCam_);
		const auto hit = wi::scene::Pick(ray, wi::enums::FILTER_OBJECT_ALL, ~0u, GetScene());
		const std::string id = hit.entity != wi::ecs::INVALID_ENTITY ? bridge_.IdOfOrAncestor(GetScene(), hit.entity) : std::string();
		ui.ed->Select(id, additive);
	}

	void EditorApp::BuildLayout(unsigned int dockId)
	{
		ImGui::DockBuilderRemoveNode(dockId);
		ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->Size);
		ImGuiID center = dockId, left, right, bottom;
		left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
		right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
		bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
		ImGui::DockBuilderDockWindow("Viewport", center);
		ImGui::DockBuilderDockWindow("Code", center);
		ImGui::DockBuilderDockWindow("Hierarchy", left);
		ImGui::DockBuilderDockWindow("Inspector", right);
		ImGui::DockBuilderDockWindow("Environment", right);
		ImGui::DockBuilderDockWindow("Assets", bottom);
		ImGui::DockBuilderDockWindow("Console", bottom);
		ImGui::DockBuilderDockWindow("Build", bottom);
		ImGui::DockBuilderDockWindow(ICON_MD_BUG_REPORT "  Debugger", bottom);
		ImGui::DockBuilderDockWindow(ICON_MD_TIMELINE "  Timeline", bottom);
		ImGui::DockBuilderDockWindow("Packages", bottom);
		ImGui::DockBuilderFinish(dockId);
	}

	void EditorApp::Update(float dt)
	{
		if (diagnosticsFirstFrame_)
			diagnostics::Stage("first frame: EditorApp::Update");
		// Idle throttle: a backgrounded editor does not need to render at monitor rate.
		if (!is_window_active && ed_->play == PlayState::Edit)
			std::this_thread::sleep_for(std::chrono::milliseconds(40));
		Editor& ed = *ed_;
		// While the Lua debugger holds the game paused this runs as a nested frame (see StartPlay): only the UI lives.
		const bool frozen = host_.DebugPaused();
		if (frozen) dt = 0;
		ed.now += frozen ? 0.016f : dt;
		// Wicked keeps last frame's camera for temporal reprojection (clouds, atmosphere LUTs, TAA, SSR): PreUpdate must run
		// BEFORE this frame's camera is changed, otherwise previous == current and everything smears while the camera moves.
		vp_.PreUpdate();
		ui::NewFrame();
		ImGuizmo::BeginFrame();
		ImGuizmo::SetGizmoSizeClipSpace(0.14f);
		ui_.ed = &ed;
		ui_.wscene = &GetScene();
		ui_.bridge = &bridge_;
		ui_.vp = &vp_;
		ui_.host = &host_;
		ui_.editCam = &editCam_;
		ui_.cam = &cam_;
		ui_.dpi = canvas.GetDPIScaling();
		ui_.debugTexts = &debugTexts_;

		if (ui_.graphicsDirty) { graphicsDirty_ = true; ui_.graphicsDirty = false; }
		if (ui_.prefsChanged)
		{
			ui_.prefsChanged = false;
			ImGui::GetIO().FontGlobalScale = ed.prefs.uiScale;
			cam_.fov = ed.prefs.cameraFov;
			cam_.speed = ed.prefs.cameraSpeed;
		}
		if (ed.prefs.autosaveMinutes > 0 && ed.HasProject() && ed.play == PlayState::Edit && (autosaveTimer_ += dt) > ed.prefs.autosaveMinutes * 60.0f)
		{
			autosaveTimer_ = 0;
			if (ed.sceneDirty)
			{
				ed.SaveAllFiles();
				const Status s = ed.SaveScene();
				ed.Toast(s ? "Autosaved" : "Autosave failed: " + s.message, !s);
			}
		}
		if (ed.HasProject())
		{
			if (graphicsDirty_)
				ApplyGraphics();
			DrawMainMenu(ui_);
			const ImGuiViewport* vpt = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(ImVec2(vpt->WorkPos.x, vpt->WorkPos.y));
			ImGui::SetNextWindowSize(ImVec2(vpt->WorkSize.x, 40));
			DrawToolbar(ui_);
			ImGui::SetNextWindowPos(ImVec2(vpt->WorkPos.x, vpt->WorkPos.y + 40));
			ImGui::SetNextWindowSize(ImVec2(vpt->WorkSize.x, vpt->WorkSize.y - 40 - 26));
			ImGui::SetNextWindowViewport(vpt->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::Begin("##dockhost", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground);
			ImGui::PopStyleVar(3);
			const ImGuiID dock = ImGui::GetID("LumenDock");
			if (!layoutBuilt_)
			{
				BuildLayout(dock);
				layoutBuilt_ = true;
			}
			ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
			ImGui::End();

			HandleShortcuts(ui_);
			if (ed.panel.viewport) DrawViewport(ui_);
			if (ed.panel.hierarchy) DrawHierarchy(ui_);
			if (ed.panel.inspector) DrawInspector(ui_);
			if (ed.panel.environment) DrawEnvironment(ui_);
			if (ed.panel.assets) DrawAssets(ui_);
			if (ed.panel.console) DrawConsole(ui_);
			if (ed.panel.code) DrawCodeEditor(ui_);
			if (ed.panel.build) DrawBuild(ui_);
			if (ed.panel.debug) DrawDebugger(ui_);
			if (ed.panel.timeline) DrawTimeline(ui_);
			if (ed.panel.packages) DrawPackages(ui_);
			if (ed.panel.libraries) DrawLibraries(ui_);
			if (ed.panel.shaders) DrawShaders(ui_);
			if (ed.panel.settings) DrawProjectSettings(ui_);
			if (ed.panel.prefs) DrawPreferences(ui_);
			DrawStatusBar(ui_);
			if (ui_.showCommandPalette) DrawCommandPalette(ui_);
		}
		else
		{
			DrawHub(ui_);
		}
		DrawToasts(ui_);
		ed.CommitFrame();
		ed.PollJobs();
		if (!ed.docs.empty() && (extTimer_ += dt) > 1.0f)
		{
			extTimer_ = 0;
			ed.CheckExternalChanges();
		}

		// play state transitions
		if (frozen)
		{
			if (ed.wantStop) host_.Debugger().Terminate(); // unwinds the paused script; the stop itself runs on a normal frame
		}
		else if (ed.wantPlay) { ed.wantPlay = false; if (ed.HasProject() && ed.play == PlayState::Edit) StartPlay(); }
		if (!frozen && ed.wantStop) { ed.wantStop = false; if (ed.play != PlayState::Edit) StopPlay(); }

		// scene -> Wicked
		if (ed.HasProject())
		{
			if (ed.scenePath != loadedScenePath_ || !sceneInWicked_)
			{
				bridge_.Build(GetScene(), ed.scene, ed.project.Root());
				loadedScenePath_ = ed.scenePath;
				sceneInWicked_ = true;
				syncedVersion_ = ed.version;
			}
			else if (ed.play == PlayState::Edit && ed.version != syncedVersion_)
			{
				bridge_.Sync(GetScene(), ed.scene);
				syncedVersion_ = ed.version;
			}
		}
		else if (sceneInWicked_)
		{
			bridge_.Clear(GetScene());
			sceneInWicked_ = false;
			loadedScenePath_.clear();
		}

		// navmesh preview (edit mode)
		if (ed.navBakeRequest)
		{
			ed.navBakeRequest = false;
			if (navPreview_.Bake(GetScene(), bridge_, ed.scene))
			{
				const nav::Stats& ns = navPreview_.Mesh().GetStats();
				ed.navInfo = std::to_string(ns.walkable) + " walkable cells, cell " + std::to_string(ns.cellSize).substr(0, 4) + " m, baked in " + std::to_string((int)ns.bakeMilliseconds) + " ms";
			}
			else
				ed.navInfo = "Nothing walkable found: mark floors as Static and check the volume.";
		}
		if (ed.navPreview && ed.play == PlayState::Edit && navPreview_.Baked())
			navPreview_.DrawDebug(GetScene(), false, true);

		UpdateCamera(ui_, dt);

		// viewport render size follows the panel
		const float dpi = std::max(1.0f, ui_.dpi);
		const uint32_t w = (uint32_t)std::max(64.0f, ui_.vpW * dpi), h = (uint32_t)std::max(64.0f, ui_.vpH * dpi);
		if (w != renderW_ || h != renderH_)
		{
			renderW_ = w;
			renderH_ = h;
		}
		vp_.init(renderW_, renderH_, 96.0f * dpi);

		// camera: editor free camera or the scene's main camera
		if (ed.play != PlayState::Edit && bridge_.MainCamera() != wi::ecs::INVALID_ENTITY)
		{
			Scene& sc = GetScene();
			if (CameraComponent* c = sc.cameras.GetComponent(bridge_.MainCamera()))
			{
				const TransformComponent* t = sc.transforms.GetComponent(bridge_.MainCamera());
				c->CreatePerspective((float)renderW_, (float)renderH_, c->zNearP, c->zFarP, c->fov);
				if (t) c->TransformCamera(*t);
				c->UpdateCamera();
				vp_.camera = c;
			}
		}
		else
		{
			cam_.Apply(editCam_, (float)renderW_, (float)renderH_);
			vp_.camera = &editCam_;
		}

		if (ed.play != PlayState::Edit && host_.Debugger().Attached())
			ApplyBreakpoints(host_.Debugger());
		if (ed.play == PlayState::Play && !frozen)
		{
			hotReloadTimer_ += dt;
			if (hotReloadTimer_ > 0.5f) { hotReloadTimer_ = 0; host_.HotReload(); }
			debugTexts_.clear();
			host_.SetScreen(ui_.vpW, ui_.vpH, ui_.vpX, ui_.vpY, ui_.dpi);
			host_.Update(dt);
		}

		vp_.Update(dt);
		vp_.PostUpdate();
		Application::Update(dt);
	}

	void EditorApp::FixedUpdate()
	{
		if (host_.DebugPaused())
			return;
		Application::FixedUpdate();
		if (ed_ && ed_->play == PlayState::Play)
			host_.FixedUpdate(1.0f / targetFrameRate);
		vp_.FixedUpdate();
	}

	void EditorApp::Render()
	{
		if (diagnosticsFirstFrame_)
			diagnostics::Stage("first frame: Wicked Application::Render");
		Application::Render();
		if (diagnosticsFirstFrame_)
			diagnostics::Stage("first frame: ViewportPath::PreRender");
		vp_.PreRender();
		if (diagnosticsFirstFrame_)
			diagnostics::Stage("first frame: ViewportPath::Render");
		vp_.Render();
		if (diagnosticsFirstFrame_)
			diagnostics::Stage("first frame: ViewportPath::PostRender");
		vp_.PostRender();
	}

	void EditorApp::Compose(wi::graphics::CommandList cmd)
	{
		if (diagnosticsFirstFrame_)
			diagnostics::Stage("first frame: EditorApp::Compose");
		Application::Compose(cmd);
		ui::Draw(cmd);
		if (diagnosticsFirstFrame_)
		{
			diagnosticsFirstFrame_ = false;
			diagnostics::Stage("first frame composed; submitting");
		}
	}
}
