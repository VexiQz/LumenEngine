// Lumen Engine — game script host: the `lumen` Lua API, script lifecycle, hot reload, play mode glue.
#pragma once
#include "Bridge.h"
#include "ClipSystem.h"
#include "NativeBuild.h"
#include "NavSystem.h"
#include "lumen/core/Project.h"
#include "lumen/core/LuaDebug.h"
#include "lumen/core/SceneDoc.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

struct lua_State;

namespace lumen
{
	struct PlayContext
	{
		const Project* project = nullptr;
		const SceneDoc* doc = nullptr;
		Bridge* bridge = nullptr;
		wi::scene::Scene* scene = nullptr;
		std::function<void(float x, float y, const std::string&)> debugText; // on screen text from lumen.debug.text
		std::function<bool(const std::string& path)> loadScene;                 // lumen.scene.load
		std::function<void(const std::string&)> quit;
		std::function<void(bool)> lockCursor;
		std::function<void(lua::Debugger&)> debuggerSetup;                     // called right after the debugger attached, before any script runs
		float viewportCenterX = 0, viewportCenterY = 0;
	};

	// One 2D primitive queued by `lumen.ui` for this frame (logical pixels of the game screen, origin top left).
	struct UiCmd
	{
		enum class Kind { Rect, Text, Image, Line } kind = Kind::Rect;
		float x = 0, y = 0, w = 0, h = 0;   // rect / image / text anchor (w,h unused for text); line: x,y -> w,h are the end point
		float color[4] = {1, 1, 1, 1};
		float rounding = 0;                 // rect corner radius
		float thickness = 1;                // line width / rect outline width (0 = filled)
		float size = 18;                    // text height
		int align = 0;                      // text: 0 left, 1 center, 2 right
		bool shadow = false;
		std::string text;                   // text content or image path
	};

	class PlayHost
	{
	public:
		~PlayHost() { Stop(); }
		// Creates the Lua state, loads the main script and attaches entity scripts. Errors are logged and returned.
		Status Start(const PlayContext& ctx);
		void Stop();
		bool Running() const { return L_ != nullptr; }
		void Update(float dt);
		void FixedUpdate(float dt);
		// Reloads scripts whose files changed on disk (state of instances is kept, functions are replaced).
		void HotReload();
		const std::vector<std::string>& Errors() const { return errors_; }
		bool CursorLocked() const { return cursorLocked_; }
	public:
		lua_State* State() const { return L_; }
		lua::Debugger& Debugger() { return debugger_; }
		NavSystem& Nav() { return nav_; }
		ClipSystem& Clips() { return clips_; }
		int PlayClip(wi::ecs::Entity root, const std::string& path, bool loop, float speed, float weight);
		bool navDebug = false; // draw the navmesh border and agent paths
		bool DebugPaused() const { return L_ && debugger_.Paused(); }
		const PlayContext& Context() const { return ctx_; }
		void AddError(const std::string& e) { errors_.push_back(e); }
		double Elapsed() const { return elapsed_; }
		float Dt() const { return dt_; }
		void SetCursorLocked(bool v);
		// The game screen the UI is laid out in: logical size, pointer offset (logical px of the area's top left corner) and DPI scale.
		void SetScreen(float w, float h, float originX, float originY, float dpi) { screenW_ = w; screenH_ = h; originX_ = originX; originY_ = originY; dpi_ = dpi; }
		float ScreenW() const { return screenW_; }
		float ScreenH() const { return screenH_; }
		const std::vector<UiCmd>& UiCommands() const { return ui_; }
		void PushUi(UiCmd c) { ui_.push_back(std::move(c)); }
		// Texture for an image path used by lumen.ui (loaded once, nullptr when missing).
		const wi::graphics::Texture* UiTexture(const std::string& path);
		void PointerLogical(float& x, float& y) const;
		// Creates the entities of a prefab asset at runtime (components, scripts, triggers) and returns its root.
		wi::ecs::Entity Instantiate(const std::string& prefabPath, const Float3* position, const Float3* rotationDegrees);
		// Smoothly changes the blend weight of an animation clip (cross fades); the clip is stopped when `stopAtEnd` and the weight reached 0.
		void FadeAnimation(wi::ecs::Entity clip, float target, float duration, bool stopAtEnd);
		void MouseDelta(float& dx, float& dy) const { dx = mdx_; dy = mdy_; }

	private:
		float dt_ = 0, mdx_ = 0, mdy_ = 0;
		lua_State* L_ = nullptr;
		lua::Debugger debugger_;
		NavSystem nav_;
		ClipSystem clips_;
		void UpdateClips(float dt);
		void NotifyEntityStr(const char* method, wi::ecs::Entity self, const std::string& arg);
		void UpdateNav(float dt);
		void AddNavAgent(const SceneEntity& se);
		void NotifyEntity(const char* method, wi::ecs::Entity self);
		PlayContext ctx_;
		std::vector<std::string> errors_;
		bool cursorLocked_ = false;
		double elapsed_ = 0;
		struct Instance { std::string entityId; std::string file; int ref = 0; uint64_t mtime = 0; bool started = false; bool failed = false; };
		std::vector<Instance> instances_;
		// Trigger volumes (rigidbody.trigger): Jolt has no sensor bodies here, so overlaps are evaluated against world bounds.
		struct Trigger { std::string id; wi::ecs::Entity e = wi::ecs::INVALID_ENTITY; Float3 offset, half; bool sphere = false; std::unordered_set<wi::ecs::Entity> inside; };
		std::vector<Trigger> triggers_;
		std::vector<UiCmd> ui_;
		std::unordered_map<std::string, wi::Resource> uiImages_;
		float screenW_ = 1280, screenH_ = 720, originX_ = 0, originY_ = 0, dpi_ = 1;
		struct Fade { wi::ecs::Entity clip; float from, to, duration, t; bool stopAtEnd; };
		std::vector<Fade> fades_;
		void UpdateFades(float dt);
		void UpdateTriggers();
		size_t AttachScripts(const SceneEntity& se);
		void AddTrigger(const SceneEntity& se);
		void NotifyTrigger(const char* method, wi::ecs::Entity self, wi::ecs::Entity other);
		NativeModuleHost native_;
		int gameRef_ = 0;
		uint64_t mainMtime_ = 0;
		void Report(const std::string& where, const char* msg);
		void Call(int ref, const char* method, int nargs, double a = 0, bool pushSelf = true);
	};
}
