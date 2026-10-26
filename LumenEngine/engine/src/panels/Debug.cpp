#include "Panels.h"

#include <algorithm>
#include <map>

namespace lumen
{
	namespace
	{
		using lua::DebugFrame;
		using lua::DebugVar;

		// Variable snapshots are taken once per pause / frame selection: every Locals()/Expand() call registers
		// references inside the Lua state, so polling them each UI frame would leak until the next resume.
		struct Snapshot
		{
			uint64_t serial = 0;
			int frame = -1;
			std::vector<DebugFrame> stack;
			std::vector<DebugVar> locals, upvalues, globals;
			bool globalsLoaded = false;
			std::map<int, std::vector<DebugVar>> children;
			std::vector<std::pair<std::string, std::string>> watches;
		};
		Snapshot& Snap()
		{
			static Snapshot s;
			return s;
		}

		std::string RelPath(const Editor& ed, const std::string& abs)
		{
			const std::string root = fs::Normalize(ed.project.Root());
			if (abs.size() > root.size() && abs.compare(0, root.size(), root) == 0)
				return abs.substr(root.size() + (abs[root.size()] == '/' ? 1 : 0));
			return abs;
		}

		void OpenAt(Editor& ed, const std::string& abs, int line)
		{
			if (abs.empty() || line <= 0)
				return;
			const std::string rel = RelPath(ed, abs);
			ed.OpenFile(rel);
			ed.panel.code = true;
			if (ed.activeDoc >= 0 && ed.activeDoc < (int)ed.docs.size())
				ed.docs[ed.activeDoc].goToLine = line;
		}

		void Rebuild(Editor& ed, lua::Debugger& d)
		{
			Snapshot& s = Snap();
			s.stack = d.Stack();
			s.frame = std::min(ed.dbg.frame, std::max(0, (int)s.stack.size() - 1));
			s.locals = d.Locals(s.frame);
			s.upvalues = d.Upvalues(s.frame);
			s.globals.clear();
			s.globalsLoaded = false;
			s.children.clear();
			s.watches.clear();
			for (const std::string& w : ed.dbg.watches)
			{
				bool ok = false;
				std::string r = d.Eval(s.frame, w, ok);
				s.watches.push_back({w, ok ? r : "<" + r + ">"});
			}
		}

		void DrawVars(lua::Debugger& d, const std::vector<DebugVar>& vars, int depth, const char* idScope)
		{
			if (depth > 8)
				return;
			Snapshot& s = Snap();
			for (const DebugVar& v : vars)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(&v);
				bool open = false;
				if (v.ref > 0)
					open = ImGui::TreeNodeEx(v.name.c_str(), ImGuiTreeNodeFlags_SpanFullWidth);
				else
					ImGui::TreeNodeEx(v.name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
				ImGui::TableNextColumn();
				ImVec4 c = ui::col::Muted();
				if (v.type == "number") c = ImVec4(0.55f, 0.80f, 0.95f, 1);
				else if (v.type == "string") c = ImVec4(0.90f, 0.72f, 0.45f, 1);
				else if (v.type == "boolean" || v.type == "nil") c = ImVec4(0.85f, 0.55f, 0.75f, 1);
				ImGui::PushStyleColor(ImGuiCol_Text, c);
				ImGui::TextUnformatted(v.value.c_str());
				ImGui::PopStyleColor();
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", v.type.c_str());
				if (open)
				{
					auto it = s.children.find(v.ref);
					if (it == s.children.end())
						it = s.children.emplace(v.ref, d.Expand(v.ref)).first;
					DrawVars(d, it->second, depth + 1, idScope);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}

		bool VarTable(const char* id)
		{
			if (!ImGui::BeginTable(id, 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV, ImVec2(0, ImGui::GetContentRegionAvail().y - 4)))
				return false;
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.32f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.52f);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.16f);
			return true;
		}
	}

	void DrawDebugger(Ui& ui)
	{
		Editor& ed = *ui.ed;
		PlayHost& host = *ui.host;
		if (!ImGui::Begin(ICON_MD_BUG_REPORT "  Debugger", &ed.panel.debug))
		{
			ImGui::End();
			return;
		}
		lua::Debugger& d = host.Debugger();
		const bool running = ed.play != PlayState::Edit && host.Running();
		const bool attached = running && d.Attached();
		const bool paused = attached && d.Paused();

		// --- pick up a new pause --------------------------------------------------------------------------
		Snapshot& snap = Snap();
		if (paused && (snap.serial != ed.dbg.pausedSerial || snap.frame != ed.dbg.frame))
		{
			snap.serial = ed.dbg.pausedSerial;
			Rebuild(ed, d);
		}
		if (!paused)
			snap = Snapshot();

		// --- toolbar --------------------------------------------------------------------------------------
		const bool f5 = ImGui::IsKeyPressed(ImGuiKey_F5, false), f10 = ImGui::IsKeyPressed(ImGuiKey_F10, false), f11 = ImGui::IsKeyPressed(ImGuiKey_F11, false);
		const bool shift = ImGui::GetIO().KeyShift;
		ImGui::BeginDisabled(!paused);
		if (ImGui::Button(ICON_MD_PLAY_ARROW "  Continue") || (paused && f5 && !shift)) d.Resume();
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_REDO "  Step over") || (paused && f10)) d.StepOver();
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_SUBDIRECTORY_ARROW_RIGHT "  Step into") || (paused && f11 && !shift)) d.StepIn();
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_ARROW_UPWARD "  Step out") || (paused && f11 && shift)) d.StepOut();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!attached || paused);
		if (ImGui::Button(ICON_MD_PAUSE "  Pause")) d.RequestPause();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!running);
		if (ImGui::Button(ICON_MD_STOP "  Stop"))
		{
			if (paused) d.Terminate();
			ed.wantStop = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::Checkbox("Follow", &ed.dbg.followPause);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the file at the paused line");

		if (!running) ImGui::TextDisabled("Not running. Click the gutter in the code editor to set breakpoints, then press Play.");
		else if (!attached) ImGui::TextColored(ui::col::Warn(), "Debugging is disabled in Project settings > Scripts (\"Allow debugger\").");
		else if (paused) ImGui::TextColored(ui::col::Warn(), ICON_MD_PAUSE_CIRCLE "  Paused (%s) at %s:%d", d.PauseReason().c_str(), RelPath(ed, d.CurrentFile()).c_str(), d.CurrentLine());
		else ImGui::TextColored(ui::col::Good(), ICON_MD_PLAY_CIRCLE "  Running");
		ImGui::Separator();

		// --- body ---------------------------------------------------------------------------------------------
		const float w = ImGui::GetContentRegionAvail().x;
		ImGui::BeginChild("##left", ImVec2(std::max(220.0f, w * 0.32f), 0), false);
		SectionLabel("Call stack");
		ImGui::BeginChild("##stack", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.5f), true);
		if (paused)
		{
			for (size_t i = 0; i < snap.stack.size(); ++i)
			{
				const DebugFrame& f = snap.stack[i];
				ImGui::PushID((int)i);
				std::string label = f.name;
				if (f.isLua && f.line > 0) label += "   " + RelPath(ed, f.source) + ":" + std::to_string(f.line);
				if (ImGui::Selectable(label.c_str(), (int)i == ed.dbg.frame))
				{
					ed.dbg.frame = (int)i;
					if (f.isLua) OpenAt(ed, f.source, f.line);
				}
				ImGui::PopID();
			}
		}
		else ImGui::TextDisabled("-");
		ImGui::EndChild();
		SectionLabel("Breakpoints");
		ImGui::BeginChild("##bps", ImVec2(0, 0), true);
		bool changed = false;
		std::string removeFile;
		int removeLine = -1;
		for (auto& f : ed.breakpoints)
			for (EdBreakpoint& b : f.second)
			{
				ImGui::PushID((f.first + std::to_string(b.line)).c_str());
				if (ImGui::Checkbox("##en", &b.enabled)) changed = true;
				ImGui::SameLine();
				if (ImGui::Selectable((f.first + ":" + std::to_string(b.line)).c_str(), false, 0, ImVec2(ImGui::GetContentRegionAvail().x - 28, 0)))
					OpenAt(ed, fs::Normalize(ed.AbsPath(f.first)), b.line);
				ImGui::SameLine();
				if (ImGui::SmallButton(ICON_MD_CLOSE)) { removeFile = f.first; removeLine = b.line; }
				char buf[160];
				std::snprintf(buf, sizeof buf, "%s", b.cond.c_str());
				ImGui::SetNextItemWidth(-1);
				if (ImGui::InputTextWithHint("##cond", "condition (Lua expression)", buf, sizeof buf)) { b.cond = buf; changed = true; }
				ImGui::PopID();
			}
		if (ed.breakpoints.empty()) ImGui::TextDisabled("No breakpoints");
		if (removeLine > 0) ed.ToggleBreakpoint(removeFile, removeLine);
		if (changed) { ++ed.bpVersion; ed.SaveBreakpoints(); }
		ImGui::EndChild();
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("##right", ImVec2(0, 0), false);
		if (ImGui::BeginTabBar("##dbgtabs"))
		{
			if (ImGui::BeginTabItem("Locals"))
			{
				if (paused && VarTable("##locals"))
				{
					DrawVars(d, snap.locals, 0, "l");
					if (!snap.upvalues.empty())
					{
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::TextDisabled("Upvalues");
						DrawVars(d, snap.upvalues, 0, "u");
					}
					ImGui::EndTable();
				}
				else if (!paused) ImGui::TextDisabled("Variables are available while paused.");
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Globals"))
			{
				if (paused)
				{
					if (!snap.globalsLoaded) { snap.globals = d.Globals(); snap.globalsLoaded = true; }
					if (VarTable("##globals")) { DrawVars(d, snap.globals, 0, "g"); ImGui::EndTable(); }
				}
				else ImGui::TextDisabled("Variables are available while paused.");
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Watch"))
			{
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
				const bool add = ImGui::InputTextWithHint("##w", "expression, Enter to add", ed.dbg.watchBuf, sizeof ed.dbg.watchBuf, ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				if ((ImGui::Button("Add") || add) && ed.dbg.watchBuf[0])
				{
					ed.dbg.watches.push_back(ed.dbg.watchBuf);
					ed.dbg.watchBuf[0] = 0;
					ed.SaveBreakpoints();
					if (paused) Rebuild(ed, d);
				}
				int del = -1;
				for (size_t i = 0; i < ed.dbg.watches.size(); ++i)
				{
					ImGui::PushID((int)i);
					if (ImGui::SmallButton(ICON_MD_CLOSE)) del = (int)i;
					ImGui::SameLine();
					ImGui::TextUnformatted(ed.dbg.watches[i].c_str());
					ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);
					if (paused && i < snap.watches.size()) ImGui::TextColored(ui::col::Muted(), "%s", snap.watches[i].second.c_str());
					ImGui::PopID();
				}
				if (del >= 0)
				{
					ed.dbg.watches.erase(ed.dbg.watches.begin() + del);
					ed.SaveBreakpoints();
					if (paused) Rebuild(ed, d);
				}
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Console"))
			{
				ImGui::BeginChild("##dbglog", ImVec2(0, ImGui::GetContentRegionAvail().y - 32), true);
				for (const std::string& l : ed.dbg.console)
					ImGui::TextUnformatted(l.c_str());
				if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
				ImGui::EndChild();
				ImGui::BeginDisabled(!paused);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::InputTextWithHint("##eval", paused ? "evaluate in the selected frame, Enter" : "pause to evaluate", ed.dbg.evalBuf, sizeof ed.dbg.evalBuf, ImGuiInputTextFlags_EnterReturnsTrue) && ed.dbg.evalBuf[0])
				{
					bool ok = false;
					const std::string r = d.Eval(ed.dbg.frame, ed.dbg.evalBuf, ok);
					ed.dbg.console.push_back("> " + std::string(ed.dbg.evalBuf));
					ed.dbg.console.push_back(ok ? r : "error: " + r);
					if (ed.dbg.console.size() > 400) ed.dbg.console.erase(ed.dbg.console.begin(), ed.dbg.console.begin() + 100);
					ed.dbg.evalBuf[0] = 0;
					ImGui::SetKeyboardFocusHere(-1);
					Rebuild(ed, d); // evaluation can change state
				}
				ImGui::EndDisabled();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::EndChild();
		ImGui::End();
	}
}
