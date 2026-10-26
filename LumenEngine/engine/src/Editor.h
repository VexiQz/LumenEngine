// Lumen Engine — editor document state: project, scene, selection, undo, open code files, play mode.
#pragma once
#include "CodeLang.h"
#include "Bridge.h"
#include "LuaEdit.h"
#include "Play.h"
#include "lumen/core/AssetDatabase.h"
#include "lumen/core/Animation.h"
#include "lumen/core/Log.h"
#include "lumen/core/NativeLibs.h"
#include "lumen/core/Packages.h"
#include "lumen/core/Project.h"
#include "lumen/core/Runtime.h"
#include "lumen/core/SceneDoc.h"
#include "lumen/core/Toolchain.h"
#include "LuaLsp.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace lumen
{
	enum class PlayState { Edit, Play, Paused };
	enum class GizmoMode { Translate, Rotate, Scale };

	struct CodeDoc
	{
		std::string path;   // project relative
		std::string text;
		std::string saved;  // text as on disk
		bool dirty() const { return text != saved; }
		std::vector<LuaDiag> diags;
		bool diagsStale = true;
		int goToLine = -1;
		uint64_t diskMtime = 0;
		bool lua = false;
		CodeLang lang = CodeLang::Plain;
	};

	struct EdBreakpoint { int line = 0; std::string cond; bool enabled = true; };

	// Background job shown in the status bar (package sync, toolchain download, native build...).
	struct Job
	{
		std::string title;
		std::atomic<float> progress{0};
		std::atomic<bool> done{false};
		std::atomic<bool> cancel{false};
		std::mutex mutex;
		std::string status;
		Status result;
		std::thread worker;
		~Job() { if (worker.joinable()) worker.join(); }
		std::string Status_() { std::lock_guard<std::mutex> l(mutex); return status; }
		void SetStatus(const std::string& s) { std::lock_guard<std::mutex> l(mutex); status = s; }
	};

	class Editor
	{
	public:
		Editor();
		~Editor();

		// ---- preferences (config/editor.json) ----------------------------------------------------
		struct Prefs
		{
			float uiScale = 1.0f;          // global UI zoom
			int autosaveMinutes = 0;       // 0 = off
			float mouseSensitivity = 1.0f; // fly / orbit camera
			float cameraFov = 60.0f;
			float cameraSpeed = 8.0f;
			bool invertY = false;
		} prefs;
		void LoadPrefs();
		Status SavePrefs() const;

		// ---- project ------------------------------------------------------------------------
		bool HasProject() const { return project.IsValid(); }
		Status NewProject(const std::string& directory, const std::string& name, const std::string& templ, bool native);
		Status OpenProject(const std::string& path);
		void CloseProject();
		std::vector<std::string> Recent() const;
		void AddRecent(const std::string& path);
		void RemoveRecent(const std::string& path);
		Status SaveProjectSettings();
		Status PersistToolchain();
		// Downloads every missing build tool (cmake/ninja/compiler) in the background. Safe to call repeatedly.
		void EnsureToolchainAsync(bool announce = false);
		bool toolchainBusy = false;
		std::mutex toolchainMutex;
		Toolchain pendingToolchain;
		std::atomic<bool> hasPendingToolchain{false};

		// ---- scene ------------------------------------------------------------------------------
		Status OpenScene(const std::string& relPath);
		Status SaveScene();
		Status NewScene(const std::string& relPath);
		void Touch();                 // call after any modification of `scene`
		void CommitFrame();           // once per UI frame: undo bookkeeping + dirty flag
		void Undo();
		void Redo();
		bool CanUndo() const { return !undo_.empty(); }
		bool CanRedo() const { return !redo_.empty(); }
		void Select(const std::string& id, bool additive = false);
		bool IsSelected(const std::string& id) const { return selection.count(id) > 0; }
		const std::string& Primary() const { return primary; }
		SceneEntity* PrimaryEntity() { return primary.empty() ? nullptr : scene.Find(primary); }
		void DeleteSelection();
		void DuplicateSelection();
		// Clipboard (system clipboard text, so it also works between scenes and projects).
		void CopySelection();
		void CutSelection();
		bool CanPaste() const;
		void Paste(const std::string& parent = "");
		void SelectAll();
		// Prefabs: entity subtree <-> .prefab asset.
		Status SaveAsPrefab(const std::string& id, std::string* outPath = nullptr);
		std::string InstantiatePrefab(const std::string& relPath, const std::string& parent = "", const Float3* position = nullptr);
		std::string CreateEntity(SceneEntity e, bool selectIt = true);
		void FocusSelection() { focusRequest = true; }

		// ---- code files ----------------------------------------------------------------------
		CodeDoc* OpenFile(const std::string& relPath, int line = -1);
		void CloseFile(size_t index);
		Status SaveFile(CodeDoc& doc);
		void SaveAllFiles();
		void CheckExternalChanges();
		std::vector<CodeDoc> docs;
		int activeDoc = -1;

		// ---- play ---------------------------------------------------------------------------------
		PlayState play = PlayState::Edit;
		bool wantPlay = false, wantStop = false;

		// ---- jobs ---------------------------------------------------------------------------------
		Job* StartJob(const std::string& title, std::function<Status(Job&)> fn);
		void PollJobs();
		std::vector<std::unique_ptr<Job>> jobs;

		// ---- toast -------------------------------------------------------------------------------
		void Toast(const std::string& text, bool error = false);
		struct ToastMsg { std::string text; bool error; double until; };
		std::vector<ToastMsg> toasts;

		// ---- state ---------------------------------------------------------------------------------
		Project project;
		AssetDatabase assets;
		SceneDoc scene;
		std::string scenePath;  // project relative
		bool sceneDirty = false;
		uint64_t version = 1; // bumped on every scene change; the app re-syncs Wicked when it differs
		std::set<std::string> selection;
		std::string primary;
		bool focusRequest = false;
		GizmoMode gizmo = GizmoMode::Translate;
		bool gizmoWorld = true;
		bool snap = false;
		float snapMove = 0.5f, snapRotate = 15.0f, snapScale = 0.1f;
		bool showGrid = true, showBounds = true, showStats = false;
		std::string assetFolder;       // current folder in the asset browser
		std::string assetSelected;
		std::shared_ptr<RingLogSink> ring;
		std::unique_ptr<NativeLibs> nativeLibs;
		EditorLsp lsp;
		// ---- terrain sculpting / navmesh preview (edit mode tools) ---------------------------------------
		struct TerrainTool { bool active = false; int brush = 0; float radius = 10.0f, strength = 0.5f; } terrainTool;
		bool navPreview = false, navBakeRequest = false;
		std::string navInfo;
		// ---- Lua debugger state (breakpoints persist per project in .lumen/breakpoints.json) ----------------
		std::map<std::string, std::vector<EdBreakpoint>> breakpoints; // project relative file -> breakpoints
		uint64_t bpVersion = 1, bpApplied = 0;
		bool HasBreakpoint(const std::string& rel, int line) const;
		void ToggleBreakpoint(const std::string& rel, int line);
		void SaveBreakpoints() const;
		void LoadBreakpoints();
		struct DebugView
		{
			bool paused = false;
			std::string file, reason;
			int line = 0;
			int frame = 0;                   // selected stack frame
			std::vector<std::string> watches;
			std::vector<std::string> console;
			char evalBuf[256] = "";
			char watchBuf[128] = "";
			bool followPause = true;         // open the file at the paused line
			uint64_t pausedSerial = 0;
		} dbg;

		// ---- animation timeline (.lanim clips; preview poses the scene document, see panels/Timeline.cpp) ------
		struct TimelineState
		{
			std::string path;                // project relative .lanim
			anim::Clip clip;
			bool loaded = false, dirty = false, playing = false, loop = true;
			float time = 0, speed = 1, zoom = 160.0f, scroll = 0;   // zoom = pixels per second
			std::string rootId;              // entity the clip is played on
			int track = -1, key = -1, event = -1;
			bool posed = false;              // the document currently holds preview poses
			std::map<std::string, SceneTransform> saved; // original transforms of posed entities
		} timeline;
		std::string timelineOpen;       // set by the asset browser, consumed by the timeline panel

		std::string AbsPath(const std::string& rel) const { return project.IsValid() ? project.Resolve(rel) : rel; }
		std::unique_ptr<ToolchainManager> toolchainMgr;
		Toolchain toolchain;
		PackageManager packages;
		std::string runtimeRoot;
		std::string lastError;
		double now = 0;
		uint64_t frame = 0;
		// Panels visibility (persisted by the layout .ini through ImGui, flags here for the View menu).
		struct Panels { bool hierarchy = true, inspector = true, assets = true, console = true, code = true, viewport = true, environment = true,
			build = true, debug = true, timeline = false, packages = true, prefs = false, libraries = false, shaders = false, settings = false, project = false, stats = false; } panel;

	private:
		void Snapshot();
		std::vector<std::string> undo_, redo_;
		std::string baseline_;
		bool touched_ = false;
		uint64_t lastTouchFrame_ = 0;
		void SelectionAfterRestore();
	};
}
