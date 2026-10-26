#include "lumen/core/LuaDebug.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Strings.h"
#include "LuaUtil.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace lumen::lua
{
	namespace
	{
		const char kDebuggerKey = 0;
		const char kRefsKey = 0;

		Debugger* Find(lua_State* L)
		{
			lua_pushlightuserdata(L, (void*)&kDebuggerKey);
			lua_rawget(L, LUA_REGISTRYINDEX);
			Debugger* d = (Debugger*)lua_touserdata(L, -1);
			lua_pop(L, 1);
			return d;
		}

		int ToStringProtected(lua_State* L)
		{
			luaL_tolstring(L, 1, nullptr);
			return 1;
		}

		bool SamePath(const std::string& a, const std::string& b)
		{
#ifdef _WIN32
			return str::EqualsIgnoreCase(a, b);
#else
			return a == b;
#endif
		}
	}

	struct DebugHookAccess
	{
		static void Hook(lua_State* L, lua_Debug* ar)
		{
			Debugger* d = Find(L);
			if (d && ar->event == LUA_HOOKLINE)
				d->OnLine(L, ar->currentline);
		}
	};

	Debugger::~Debugger() { Detach(); }

	std::string Debugger::NormalizePath(const std::string& path)
	{
		std::string p = path;
		if (!p.empty() && (p[0] == '@' || p[0] == '='))
			p.erase(0, 1);
		return fs::Normalize(p);
	}

	// ----------------------------------------------------------------------------------------------------
	void Debugger::Attach(lua_State* L)
	{
		if (!L)
			return;
		L_ = L;
		lua_pushlightuserdata(L, (void*)&kDebuggerKey);
		lua_pushlightuserdata(L, this);
		lua_rawset(L, LUA_REGISTRYINDEX);
		Refresh();
	}

	void Debugger::Detach()
	{
		if (!L_)
			return;
		paused_ = false; // releases a blocked hook
		resumeFlag_ = true;
		lua_sethook(L_, nullptr, 0, 0);
		lua_pushlightuserdata(L_, (void*)&kDebuggerKey);
		lua_pushnil(L_);
		lua_rawset(L_, LUA_REGISTRYINDEX);
		L_ = nullptr;
	}

	bool Debugger::NeedsHook() const
	{
		return pauseRequested_ || mode_ != (int)Mode::Run || !bpLines_.empty();
	}

	void Debugger::Refresh()
	{
		if (!L_)
			return;
		if (NeedsHook())
			lua_sethook(L_, &DebugHookAccess::Hook, LUA_MASKLINE, 0);
		else
			lua_sethook(L_, nullptr, 0, 0);
	}

	// ---- breakpoints -----------------------------------------------------------------------------------
	void Debugger::SetBreakpoint(const std::string& file, int line, const std::string& condition)
	{
		{
			std::lock_guard<std::mutex> l(mutex_);
			Breakpoint b;
			b.line = line;
			b.condition = condition;
			bps_[NormalizePath(file)][line] = b;
			bpLines_.insert(line);
		}
		Refresh();
	}

	bool Debugger::ToggleBreakpoint(const std::string& file, int line)
	{
		if (HasBreakpoint(file, line))
		{
			RemoveBreakpoint(file, line);
			return false;
		}
		SetBreakpoint(file, line);
		return true;
	}

	void Debugger::RemoveBreakpoint(const std::string& file, int line)
	{
		{
			std::lock_guard<std::mutex> l(mutex_);
			auto it = bps_.find(NormalizePath(file));
			if (it != bps_.end())
			{
				it->second.erase(line);
				if (it->second.empty())
					bps_.erase(it);
			}
			bpLines_.clear();
			for (auto& f : bps_)
				for (auto& b : f.second)
					bpLines_.insert(b.first);
		}
		Refresh();
	}

	void Debugger::ClearBreakpoints()
	{
		{
			std::lock_guard<std::mutex> l(mutex_);
			bps_.clear();
			bpLines_.clear();
		}
		Refresh();
	}

	std::vector<std::pair<std::string, Breakpoint>> Debugger::Breakpoints() const
	{
		std::lock_guard<std::mutex> l(mutex_);
		std::vector<std::pair<std::string, Breakpoint>> out;
		for (auto& f : bps_)
			for (auto& b : f.second)
				out.push_back({f.first, b.second});
		std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.first == b.first ? a.second.line < b.second.line : a.first < b.first; });
		return out;
	}

	bool Debugger::HasBreakpoint(const std::string& file, int line) const
	{
		std::lock_guard<std::mutex> l(mutex_);
		auto it = bps_.find(NormalizePath(file));
		return it != bps_.end() && it->second.count(line) > 0;
	}

	// ---- run control -----------------------------------------------------------------------------------
	void Debugger::Step(Mode m)
	{
		mode_ = (int)m;
		if (m == Mode::StepIn || m == Mode::StepOver || m == Mode::StepOut)
		{
			stepThread_ = paused_L_;
			stepDepth_ = paused_L_ ? StackDepth(paused_L_) : 0;
		}
		pauseRequested_ = false;
		resumeFlag_ = true;
		paused_ = false;
		Refresh();
	}

	int Debugger::StackDepth(lua_State* L) const
	{
		int n = 0;
		lua_Debug ar;
		while (lua_getstack(L, n, &ar))
			++n;
		return n;
	}

	// ---- hook ---------------------------------------------------------------------------------------------
	void Debugger::OnLine(lua_State* L, int line)
	{
		bool stop = false;
		std::string reason;
		const Mode m = (Mode)mode_.load();
		std::string file;
		auto currentFile = [&]() {
			if (!file.empty())
				return file;
			lua_Debug ar;
			if (lua_getstack(L, 0, &ar) && lua_getinfo(L, "S", &ar))
				file = NormalizePath(ar.source ? ar.source : "");
			return file;
		};

		if (pauseRequested_)
		{
			stop = true;
			reason = reasonOnce_.empty() ? "pause" : reasonOnce_;
			reasonOnce_.clear();
			pauseRequested_ = false;
		}
		else if (m == Mode::StepIn)
		{
			stop = true;
			reason = "step";
		}
		else if ((m == Mode::StepOver || m == Mode::StepOut) && L == stepThread_)
		{
			const int depth = StackDepth(L);
			if (m == Mode::StepOver ? depth <= stepDepth_ : depth < stepDepth_)
			{
				stop = true;
				reason = "step";
			}
		}
		if (!stop && bpLines_.count(line))
		{
			const std::string f = currentFile();
			std::string cond;
			bool hit = false;
			{
				std::lock_guard<std::mutex> l(mutex_);
				auto it = bps_.find(f);
				if (it == bps_.end())
				{
					for (auto& e : bps_)
						if (SamePath(e.first, f)) { it = bps_.find(e.first); break; }
				}
				if (it != bps_.end())
				{
					auto b = it->second.find(line);
					if (b != it->second.end() && b->second.enabled)
					{
						cond = b->second.condition;
						hit = true;
					}
				}
			}
			if (hit && (cond.empty() || ConditionTrue(L, cond)))
			{
				stop = true;
				reason = "breakpoint";
				std::lock_guard<std::mutex> l(mutex_);
				auto it = bps_.find(f);
				if (it != bps_.end() && it->second.count(line))
					it->second[line].hits++;
			}
		}
		if (stop)
			PauseHere(L, currentFile(), line, reason);
	}

	void Debugger::PauseHere(lua_State* L, const std::string& file, int line, const std::string& reason)
	{
		paused_L_ = L;
		pausedFile_ = file;
		pausedLine_ = line;
		reason_ = reason;
		mode_ = (int)Mode::Run;
		resumeFlag_ = false;
		paused_ = true;
		if (onPause)
			onPause(*this);
		while (paused_ && L_)
		{
			if (pump)
				pump();
			else
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		ClearRefs();
		paused_L_ = nullptr;
		if (onResume)
			onResume();
		if (terminate_.exchange(false))
			luaL_error(L, "terminated by debugger");
	}

	bool Debugger::ConditionTrue(lua_State* L, const std::string& cond)
	{
		// paused_L_ is needed by the inspection helpers
		lua_State* saved = paused_L_;
		paused_L_ = L;
		bool ok = false;
		const std::string r = Eval(0, cond, ok);
		paused_L_ = saved;
		return ok && r != "false" && r != "nil";
	}

	// ---- inspection ----------------------------------------------------------------------------------------
	std::string Debugger::Describe(lua_State* L, int idx, int maxLen)
	{
		idx = lua_absindex(L, idx);
		switch (lua_type(L, idx))
		{
		case LUA_TNIL: return "nil";
		case LUA_TBOOLEAN: return lua_toboolean(L, idx) ? "true" : "false";
		case LUA_TNUMBER:
		{
			char b[64];
			if (lua_isinteger(L, idx))
				std::snprintf(b, sizeof b, "%lld", (long long)lua_tointeger(L, idx));
			else
				std::snprintf(b, sizeof b, "%.14g", (double)lua_tonumber(L, idx));
			return b;
		}
		case LUA_TSTRING:
		{
			size_t n = 0;
			const char* s = lua_tolstring(L, idx, &n);
			std::string v(s, std::min<size_t>(n, (size_t)maxLen));
			for (char& c : v)
				if (c == '\n' || c == '\r' || c == '\t') c = ' ';
			return "\"" + v + (n > (size_t)maxLen ? "\"..." : "\"");
		}
		case LUA_TTABLE:
		{
			size_t count = 0;
			lua_pushnil(L);
			while (lua_next(L, idx) && count < 100000)
			{
				++count;
				lua_pop(L, 1);
			}
			char b[64];
			std::snprintf(b, sizeof b, "table (%zu)", count);
			return b;
		}
		case LUA_TFUNCTION:
		{
			char b[64];
			std::snprintf(b, sizeof b, lua_iscfunction(L, idx) ? "C function: %p" : "function: %p", lua_topointer(L, idx));
			return b;
		}
		case LUA_TUSERDATA:
		{
			lua_pushcfunction(L, ToStringProtected);
			lua_pushvalue(L, idx);
			std::string r = "userdata";
			if (lua_pcall(L, 1, 1, 0) == LUA_OK)
			{
				r = lua_tostring(L, -1) ? lua_tostring(L, -1) : "userdata";
				lua_pop(L, 1);
			}
			else
				lua_pop(L, 1);
			return r;
		}
		default:
		{
			char b[64];
			std::snprintf(b, sizeof b, "%s: %p", luaL_typename(L, idx), lua_topointer(L, idx));
			return b;
		}
		}
	}

	int Debugger::NewRef(lua_State* L, int idx)
	{
		if (!refsOpen_)
		{
			lua_pushlightuserdata(L, (void*)&kRefsKey);
			lua_newtable(L);
			lua_rawset(L, LUA_REGISTRYINDEX);
			refsOpen_ = true;
			nextRef_ = 1;
		}
		lua_pushlightuserdata(L, (void*)&kRefsKey);
		lua_rawget(L, LUA_REGISTRYINDEX);
		lua_pushvalue(L, idx);
		const int ref = nextRef_++;
		lua_rawseti(L, -2, ref);
		lua_pop(L, 1);
		return ref;
	}

	void Debugger::ClearRefs()
	{
		if (!refsOpen_ || !L_)
			return;
		lua_pushlightuserdata(L_, (void*)&kRefsKey);
		lua_pushnil(L_);
		lua_rawset(L_, LUA_REGISTRYINDEX);
		refsOpen_ = false;
	}

	void Debugger::FillVar(lua_State* L, int idx, DebugVar& v)
	{
		idx = lua_absindex(L, idx);
		v.type = luaL_typename(L, idx);
		v.value = Describe(L, idx);
		if (lua_type(L, idx) == LUA_TTABLE)
		{
			lua_pushnil(L);
			if (lua_next(L, idx))
			{
				lua_pop(L, 2);
				v.ref = NewRef(L, idx);
			}
		}
	}

	std::vector<DebugFrame> Debugger::Stack()
	{
		std::vector<DebugFrame> out;
		lua_State* L = paused_L_;
		if (!L)
			return out;
		lua_Debug ar;
		for (int level = 0; lua_getstack(L, level, &ar); ++level)
		{
			lua_getinfo(L, "nSl", &ar);
			DebugFrame f;
			f.isLua = ar.what && std::string(ar.what) != "C";
			f.source = f.isLua ? NormalizePath(ar.source ? ar.source : "") : std::string();
			f.line = ar.currentline > 0 ? ar.currentline : 0;
			if (ar.name)
				f.name = ar.name;
			else if (ar.what && std::string(ar.what) == "main")
				f.name = "main chunk";
			else
				f.name = ar.what && std::string(ar.what) == "C" ? "[C]" : "?";
			out.push_back(std::move(f));
		}
		return out;
	}

	std::vector<DebugVar> Debugger::Locals(int frame)
	{
		std::vector<DebugVar> out;
		lua_State* L = paused_L_;
		lua_Debug ar;
		if (!L || !lua_getstack(L, frame, &ar))
			return out;
		for (int i = 1;; ++i)
		{
			const char* name = lua_getlocal(L, &ar, i);
			if (!name)
				break;
			if (name[0] != '(' || std::string(name) == "(vararg)")
			{
				DebugVar v;
				v.name = name;
				FillVar(L, -1, v);
				out.push_back(std::move(v));
			}
			lua_pop(L, 1);
		}
		return out;
	}

	std::vector<DebugVar> Debugger::Upvalues(int frame)
	{
		std::vector<DebugVar> out;
		lua_State* L = paused_L_;
		lua_Debug ar;
		if (!L || !lua_getstack(L, frame, &ar) || !lua_getinfo(L, "f", &ar))
			return out;
		for (int i = 1;; ++i)
		{
			const char* name = lua_getupvalue(L, -1, i);
			if (!name)
				break;
			if (std::string(name) != "_ENV")
			{
				DebugVar v;
				v.name = name;
				FillVar(L, -1, v);
				out.push_back(std::move(v));
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		return out;
	}

	std::vector<DebugVar> Debugger::Globals()
	{
		std::vector<DebugVar> out;
		lua_State* L = paused_L_;
		if (!L)
			return out;
		lua_pushglobaltable(L);
		const int t = lua_gettop(L);
		lua_pushnil(L);
		while (lua_next(L, t))
		{
			if (lua_type(L, -2) == LUA_TSTRING)
			{
				DebugVar v;
				v.name = lua_tostring(L, -2);
				if (lua_type(L, -1) != LUA_TFUNCTION && v.name != "_G" && v.name != "package")
				{
					FillVar(L, -1, v);
					out.push_back(std::move(v));
				}
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		std::sort(out.begin(), out.end(), [](const DebugVar& a, const DebugVar& b) { return a.name < b.name; });
		return out;
	}

	std::vector<DebugVar> Debugger::Expand(int ref)
	{
		std::vector<DebugVar> out;
		lua_State* L = paused_L_;
		if (!L || !refsOpen_)
			return out;
		lua_pushlightuserdata(L, (void*)&kRefsKey);
		lua_rawget(L, LUA_REGISTRYINDEX);
		lua_rawgeti(L, -1, ref);
		if (lua_type(L, -1) == LUA_TTABLE)
		{
			const int t = lua_gettop(L);
			lua_pushnil(L);
			int n = 0;
			while (lua_next(L, t) && n < 300)
			{
				DebugVar v;
				if (lua_type(L, -2) == LUA_TSTRING)
					v.name = lua_tostring(L, -2);
				else
				{
					v.name = "[" + Describe(L, -2, 40) + "]";
				}
				FillVar(L, -1, v);
				out.push_back(std::move(v));
				lua_pop(L, 1);
				++n;
			}
			if (lua_getmetatable(L, t))
			{
				DebugVar v;
				v.name = "(metatable)";
				FillVar(L, -1, v);
				out.push_back(std::move(v));
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 2);
		std::sort(out.begin(), out.end(), [](const DebugVar& a, const DebugVar& b) {
			const bool an = !a.name.empty() && a.name[0] == '[', bn = !b.name.empty() && b.name[0] == '[';
			if (an != bn) return an;
			if (an) return a.name.size() == b.name.size() ? a.name < b.name : a.name.size() < b.name.size();
			return a.name < b.name;
		});
		return out;
	}

	// Builds an environment table with the frame's locals and upvalues, falling back to _G.
	int Debugger::PushEnv(lua_State* L, int frame)
	{
		lua_newtable(L);
		const int env = lua_gettop(L);
		lua_Debug ar;
		if (lua_getstack(L, frame, &ar))
		{
			if (lua_getinfo(L, "f", &ar))
			{
				for (int i = 1;; ++i)
				{
					const char* name = lua_getupvalue(L, -1, i);
					if (!name)
						break;
					lua_setfield(L, env, name);
				}
				lua_pop(L, 1);
			}
			for (int i = 1;; ++i)
			{
				const char* name = lua_getlocal(L, &ar, i);
				if (!name)
					break;
				if (name[0] != '(')
					lua_setfield(L, env, name);
				else
					lua_pop(L, 1);
			}
		}
		lua_newtable(L);
		lua_pushglobaltable(L);
		lua_setfield(L, -2, "__index");
		lua_setmetatable(L, env);
		return env;
	}

	std::string Debugger::Eval(int frame, const std::string& expression, bool& ok)
	{
		lua_State* L = paused_L_;
		ok = false;
		if (!L)
			return "not paused";
		const int top = lua_gettop(L);
		PushEnv(L, frame);
		const int env = lua_gettop(L);
		std::string result;
		const std::string code = "return " + expression;
		if (luaL_loadbufferx(L, code.data(), code.size(), "=(eval)", "t") != LUA_OK)
		{
			// statement instead of expression
			lua_pop(L, 1);
			if (luaL_loadbufferx(L, expression.data(), expression.size(), "=(eval)", "t") != LUA_OK)
			{
				result = lua_tostring(L, -1) ? lua_tostring(L, -1) : "syntax error";
				lua_settop(L, top);
				return result;
			}
		}
		lua_pushvalue(L, env);
		if (!lua_setupvalue(L, -2, 1))
			lua_pop(L, 1);
		if (lua_pcall(L, 0, 1, 0) != LUA_OK)
		{
			result = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
			lua_settop(L, top);
			return result;
		}
		result = Describe(L, -1);
		ok = true;
		lua_settop(L, top);
		return result;
	}

} // namespace lumen::lua
