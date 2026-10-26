// Lumen Engine — Lua debugger core: breakpoints (with conditions), pause, step in/over/out, call stack,
// locals/upvalues/globals inspection with expandable tables, expression evaluation.
//
// The debugger runs inside the Lua hook of the thread that executes the script. When it pauses it blocks in
// the hook and calls `pump` in a loop (the host keeps its UI/message loop alive there) until Resume/Step*.
#pragma once

#include "lumen/core/Common.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

struct lua_State;

namespace lumen::lua
{
	struct DebugFrame
	{
		std::string name;     // function name or "main chunk"
		std::string source;   // normalised file path ("" for C functions / strings)
		int line = 0;         // current line (1 based), 0 for C functions
		bool isLua = true;
	};

	struct DebugVar
	{
		std::string name;
		std::string value;    // short display text
		std::string type;     // Lua type name
		int ref = 0;          // > 0: expandable (table); pass to Debugger::Expand while paused
	};

	struct Breakpoint
	{
		int line = 0;
		std::string condition; // Lua expression evaluated in the frame; empty = always
		bool enabled = true;
		int hits = 0;
	};

	class Debugger
	{
	public:
		enum class Mode { Run, StepIn, StepOver, StepOut, Pause };

		Debugger() = default;
		~Debugger();
		Debugger(const Debugger&) = delete;
		Debugger& operator=(const Debugger&) = delete;

		// Installs the hook on `L` (coroutines created later inherit it). Safe to call again.
		void Attach(lua_State* L);
		void Detach();
		bool Attached() const { return L_ != nullptr; }

		// --- breakpoints (paths are compared normalised, case-insensitive on Windows) ----------------------
		void SetBreakpoint(const std::string& file, int line, const std::string& condition = {});
		bool ToggleBreakpoint(const std::string& file, int line); // returns true when now set
		void RemoveBreakpoint(const std::string& file, int line);
		void ClearBreakpoints();
		std::vector<std::pair<std::string, Breakpoint>> Breakpoints() const;
		bool HasBreakpoint(const std::string& file, int line) const;

		// --- run control (thread safe; Pause may be called from any thread) --------------------------------
		void RequestPause() { pauseRequested_ = true; Refresh(); }
		void Resume() { Step(Mode::Run); }
		void StepIn() { Step(Mode::StepIn); }
		void StepOver() { Step(Mode::StepOver); }
		void StepOut() { Step(Mode::StepOut); }
		bool Paused() const { return paused_; }
		// Aborts the paused script with a Lua error ("terminated by debugger") so the host can stop the game safely.
		void Terminate() { terminate_ = true; paused_ = false; }
		const std::string& PauseReason() const { return reason_; }
		// Called by Lua code (lumen.breakpoint()) or a host to stop at the next executed line.
		void BreakNow(const char* reason = "breakpoint()") { reasonOnce_ = reason; pauseRequested_ = true; Refresh(); }

		// --- inspection (valid only while Paused(), from the pumping thread) -------------------------------
		std::vector<DebugFrame> Stack();
		std::vector<DebugVar> Locals(int frame);
		std::vector<DebugVar> Upvalues(int frame);
		std::vector<DebugVar> Globals();
		std::vector<DebugVar> Expand(int ref);
		// Evaluates an expression in the scope of `frame` (locals, upvalues, globals). On failure ok=false and
		// the returned text is the error message.
		std::string Eval(int frame, const std::string& expression, bool& ok);

		// --- host hooks ---------------------------------------------------------------------------------
		std::function<void(Debugger&)> onPause;   // once, when execution stops
		std::function<void()> onResume;           // once, when execution continues
		std::function<void()> pump;               // repeatedly while paused; if empty the thread sleeps 5 ms
		const std::string& CurrentFile() const { return pausedFile_; }
		int CurrentLine() const { return pausedLine_; }

		static std::string NormalizePath(const std::string& path);

	private:
		friend struct DebugHookAccess;
		void OnLine(lua_State* L, int line);
		void Step(Mode m);
		void Refresh();            // re-apply hook mask
		bool NeedsHook() const;
		void PauseHere(lua_State* L, const std::string& file, int line, const std::string& reason);
		int StackDepth(lua_State* L) const;
		bool ConditionTrue(lua_State* L, const std::string& cond);
		int NewRef(lua_State* L, int idx);
		void ClearRefs();
		void FillVar(lua_State* L, int idx, DebugVar& v);
		static std::string Describe(lua_State* L, int idx, int maxLen = 160);
		int PushEnv(lua_State* L, int frame);

		lua_State* L_ = nullptr;          // main attached state
		lua_State* paused_L_ = nullptr;   // thread currently stopped
		mutable std::mutex mutex_;
		std::unordered_map<std::string, std::unordered_map<int, Breakpoint>> bps_;
		std::unordered_set<int> bpLines_;  // fast pre-filter
		std::atomic<bool> paused_{false};
		std::atomic<bool> pauseRequested_{false};
		std::atomic<int> mode_{(int)Mode::Run};
		std::atomic<bool> resumeFlag_{false};
		std::atomic<bool> terminate_{false};
		int stepDepth_ = 0;
		lua_State* stepThread_ = nullptr;
		std::string reason_, reasonOnce_;
		std::string pausedFile_;
		int pausedLine_ = 0;
		int nextRef_ = 1;
		bool refsOpen_ = false;
	};

} // namespace lumen::lua
