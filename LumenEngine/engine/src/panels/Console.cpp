#include "Panels.h"

#include <algorithm>

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#include "lumen/core/LuaLibs.h"

namespace lumen
{
	namespace
	{
		// REPL state: a Lua state with the Lumen native libs, created lazily and kept for the session.
		struct Repl
		{
			lua_State* L = nullptr;
			std::vector<std::string> history;
			int histPos = -1;
			~Repl() { if (L) lua_close(L); }
			void Ensure(const Project& p)
			{
				if (L) return;
				L = luaL_newstate();
				luaL_openlibs(L);
				lua::LibsPolicy policy;
				lua::OpenLibs(L);
				std::string path = p.IsValid() ? p.Root() + "/?.lua;" + p.Resolve("scripts") + "/?.lua;" : "";
				if (Runtime::IsInitialized()) path += Runtime::Layout().lua + "/?.lua;" + Runtime::Layout().lua + "/?/init.lua;";
				lua_getglobal(L, "package");
				lua_pushstring(L, path.c_str());
				lua_setfield(L, -2, "path");
				lua_pop(L, 1);
			}
			std::string Run(const std::string& code)
			{
				std::string out;
				// capture print()
				static std::string* sink = nullptr;
				sink = &out;
				lua_pushcfunction(L, [](lua_State* L) -> int {
					std::string line;
					const int n = lua_gettop(L);
					for (int i = 1; i <= n; ++i) { size_t l; const char* s = luaL_tolstring(L, i, &l); if (i > 1) line += '\t'; line.append(s, l); lua_pop(L, 1); }
					*sink += line + "\n";
					return 0;
				});
				lua_setglobal(L, "print");
				std::string src = "return " + code;
				if (luaL_loadstring(L, src.c_str()) != LUA_OK)
				{
					lua_pop(L, 1);
					if (luaL_loadstring(L, code.c_str()) != LUA_OK)
					{
						out += std::string(lua_tostring(L, -1)) + "\n";
						lua_pop(L, 1);
						return out;
					}
				}
				const int base = lua_gettop(L) - 1;
				if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
				{
					out += std::string(lua_tostring(L, -1)) + "\n";
					lua_pop(L, 1);
					return out;
				}
				for (int i = base + 1; i <= lua_gettop(L); ++i)
				{
					size_t l;
					const char* s = luaL_tolstring(L, i, &l);
					out.append(s, l);
					out += "\n";
					lua_pop(L, 1);
				}
				lua_settop(L, base);
				return out;
			}
		};
	}

	void DrawConsole(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::Begin("Console", &ed.panel.console))
		{
			ImGui::End();
			return;
		}
		static Repl repl;
		static bool showLevel[6] = {false, true, true, true, true, true};
		static char filter[64] = "";
		static bool autoScroll = true;
		static std::vector<std::string> replOutput;
		static char input[512] = "";
		static bool replMode = false;

		if (ImGui::Button(ICON_MD_DELETE " Clear")) { ed.ring->Clear(); replOutput.clear(); }
		ImGui::SameLine();
		const char* names[] = {"Trace", "Debug", "Info", "Warn", "Error", "Fatal"};
		static const ImVec4 colors[] = {{0.5f, 0.5f, 0.55f, 1}, {0.6f, 0.65f, 0.72f, 1}, {0.86f, 0.87f, 0.89f, 1}, {0.93f, 0.78f, 0.36f, 1}, {0.92f, 0.42f, 0.40f, 1}, {1, 0.3f, 0.3f, 1}};
		for (int i = 1; i <= 4; ++i)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colors[i]);
			ImGui::Checkbox(names[i], &showLevel[i]);
			ImGui::PopStyleColor();
			ImGui::SameLine();
		}
		ImGui::SetNextItemWidth(160);
		ImGui::InputTextWithHint("##flt", ICON_MD_SEARCH "  filter", filter, sizeof(filter));
		ImGui::SameLine();
		ImGui::Checkbox("Lua REPL", &replMode);
		ImGui::SameLine();
		ImGui::Checkbox("Follow", &autoScroll);
		ImGui::Separator();

		const float footer = replMode ? 34 : 0;
		ImGui::BeginChild("log", ImVec2(0, -footer), false, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::PushFont(ui::MonoFont());
		for (const LogRecord& r : ed.ring->Snapshot())
		{
			const int lv = (int)r.level;
			if (lv < 5 && !showLevel[lv]) continue;
			if (filter[0] && r.message.find(filter) == std::string::npos && r.channel.find(filter) == std::string::npos) continue;
			char tbuf[16];
			const time_t t = (time_t)(r.unixMs / 1000);
			struct tm tmv;
#ifdef _WIN32
			localtime_s(&tmv, &t);
#else
			localtime_r(&t, &tmv);
#endif
			strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tmv);
			ImGui::PushStyleColor(ImGuiCol_Text, colors[std::min(lv, 5)]);
			ImGui::TextDisabled("%s", tbuf);
			ImGui::SameLine();
			ImGui::TextDisabled("%-8s", r.channel.c_str());
			ImGui::SameLine();
			ImGui::TextUnformatted(r.message.c_str());
			ImGui::PopStyleColor();
			if (ImGui::IsItemClicked(1)) ImGui::SetClipboardText(r.message.c_str());
		}
		for (const std::string& l : replOutput)
			ImGui::TextUnformatted(l.c_str());
		ImGui::PopFont();
		if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();

		if (replMode)
		{
			ImGui::SetNextItemWidth(-1);
			ImGui::PushFont(ui::MonoFont());
			const bool enter = ImGui::InputTextWithHint("##repl", "lua>  expression or statement, e.g. lumen.mathx.lerp(0, 10, 0.3)", input, sizeof(input), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::PopFont();
			if (enter && input[0])
			{
				repl.Ensure(ed.project);
				replOutput.push_back(std::string("> ") + input);
				const std::string out = repl.Run(input);
				for (const std::string& line : str::SplitLines(out))
					if (!line.empty()) replOutput.push_back(line);
				if (replOutput.size() > 2000) replOutput.erase(replOutput.begin(), replOutput.begin() + 500);
				input[0] = 0;
				ImGui::SetKeyboardFocusHere(-1);
			}
		}
		ImGui::End();
	}
}
