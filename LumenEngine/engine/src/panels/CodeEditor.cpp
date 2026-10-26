#include "Panels.h"
#include "../CodeLang.h"
#include "../LuaLsp.h"
#include <cstring>

#include <algorithm>
#include <map>

namespace lumen
{
	namespace
	{
		struct View
		{
			std::vector<std::string> lines{""};
			std::string synced;                 // doc.text the lines were built from
			int cl = 0, cc = 0, al = 0, ac = 0; // cursor and selection anchor (line, byte column)
			float wantX = -1;
			bool dragging = false;
			std::vector<std::string> undo, redo;
			double lastEditTime = 0;
			double lastCheck = 0;
			bool hlDirty = true;
			std::vector<std::vector<LuaSpan>> spans;
			// completion
			bool compOpen = false;
			std::vector<LuaCompletion> comps;
			int compSel = 0;
			int compStart = 0;
			// find
			bool findOpen = false;
			char findText[128] = "";
			char replaceText[128] = "";
			bool scrollToCursor = false;
		};

		std::map<std::string, View>& Views()
		{
			static std::map<std::string, View> v;
			return v;
		}

		void Split(View& v, const std::string& text)
		{
			v.lines.clear();
			size_t s = 0;
			while (true)
			{
				const size_t e = text.find('\n', s);
				std::string l = text.substr(s, e == std::string::npos ? std::string::npos : e - s);
				if (!l.empty() && l.back() == '\r') l.pop_back();
				v.lines.push_back(std::move(l));
				if (e == std::string::npos) break;
				s = e + 1;
			}
			v.hlDirty = true;
		}
		std::string Join(const View& v)
		{
			std::string out;
			for (size_t i = 0; i < v.lines.size(); ++i)
			{
				out += v.lines[i];
				if (i + 1 < v.lines.size()) out += '\n';
			}
			return out;
		}

		bool HasSel(const View& v) { return v.cl != v.al || v.cc != v.ac; }
		void OrderSel(const View& v, int& l0, int& c0, int& l1, int& c1)
		{
			if (v.al < v.cl || (v.al == v.cl && v.ac <= v.cc)) { l0 = v.al; c0 = v.ac; l1 = v.cl; c1 = v.cc; }
			else { l0 = v.cl; c0 = v.cc; l1 = v.al; c1 = v.ac; }
		}
		std::string SelText(const View& v)
		{
			if (!HasSel(v)) return {};
			int l0, c0, l1, c1;
			OrderSel(v, l0, c0, l1, c1);
			if (l0 == l1) return v.lines[l0].substr(c0, c1 - c0);
			std::string out = v.lines[l0].substr(c0) + "\n";
			for (int l = l0 + 1; l < l1; ++l) out += v.lines[l] + "\n";
			return out + v.lines[l1].substr(0, c1);
		}
		void DeleteSel(View& v)
		{
			if (!HasSel(v)) return;
			int l0, c0, l1, c1;
			OrderSel(v, l0, c0, l1, c1);
			v.lines[l0] = v.lines[l0].substr(0, c0) + v.lines[l1].substr(c1);
			v.lines.erase(v.lines.begin() + l0 + 1, v.lines.begin() + l1 + 1);
			v.cl = v.al = l0;
			v.cc = v.ac = c0;
		}
		void InsertText(View& v, const std::string& text)
		{
			DeleteSel(v);
			std::string tail = v.lines[v.cl].substr(v.cc);
			v.lines[v.cl].resize(v.cc);
			size_t s = 0;
			int line = v.cl;
			while (true)
			{
				const size_t e = text.find('\n', s);
				std::string part = text.substr(s, e == std::string::npos ? std::string::npos : e - s);
				if (!part.empty() && part.back() == '\r') part.pop_back();
				if (line == v.cl) v.lines[line] += part;
				else v.lines.insert(v.lines.begin() + line, part);
				if (e == std::string::npos) break;
				s = e + 1;
				++line;
				if (line > v.cl && (size_t)line > v.lines.size()) v.lines.resize(line);
			}
			v.cl = line;
			v.cc = (int)v.lines[line].size();
			v.lines[line] += tail;
			v.al = v.cl;
			v.ac = v.cc;
		}
		bool IsWord(char c) { return std::isalnum((unsigned char)c) || c == '_'; }
		int PrevWord(const std::string& s, int c)
		{
			while (c > 0 && !IsWord(s[c - 1])) --c;
			while (c > 0 && IsWord(s[c - 1])) --c;
			return c;
		}
		int NextWord(const std::string& s, int c)
		{
			const int n = (int)s.size();
			while (c < n && !IsWord(s[c])) ++c;
			while (c < n && IsWord(s[c])) ++c;
			return c;
		}
		int Utf8Prev(const std::string& s, int c) { if (c <= 0) return 0; --c; while (c > 0 && (s[c] & 0xC0) == 0x80) --c; return c; }
		int Utf8Next(const std::string& s, int c) { const int n = (int)s.size(); if (c >= n) return n; ++c; while (c < n && (s[c] & 0xC0) == 0x80) ++c; return c; }

		float TextW(const std::string& s, int upto) { return upto <= 0 ? 0.0f : ImGui::CalcTextSize(s.c_str(), s.c_str() + std::min<int>(upto, (int)s.size())).x; }
		int ColAtX(const std::string& s, float x)
		{
			int c = 0;
			while (c < (int)s.size())
			{
				const int n = Utf8Next(s, c);
				const float w0 = TextW(s, c), w1 = TextW(s, n);
				if (x < (w0 + w1) * 0.5f) return c;
				c = n;
			}
			return c;
		}

		ImU32 ColorFor(LuaTok t)
		{
			switch (t)
			{
			case LuaTok::Keyword: return IM_COL32(222, 134, 112, 255);
			case LuaTok::Builtin: return IM_COL32(120, 190, 220, 255);
			case LuaTok::String: return IM_COL32(150, 200, 120, 255);
			case LuaTok::Number: return IM_COL32(230, 190, 110, 255);
			case LuaTok::Comment: return IM_COL32(110, 120, 135, 255);
			case LuaTok::Operator: return IM_COL32(170, 175, 190, 255);
			case LuaTok::Lumen: return IM_COL32(245, 158, 64, 255);
			case LuaTok::Type: return IM_COL32(110, 205, 190, 255);
			case LuaTok::Preproc: return IM_COL32(190, 140, 220, 255);
			case LuaTok::Function: return IM_COL32(235, 214, 150, 255);
			case LuaTok::Constant: return IM_COL32(210, 150, 200, 255);
			case LuaTok::Heading: return IM_COL32(245, 158, 64, 255);
			case LuaTok::Emphasis: return IM_COL32(200, 205, 150, 255);
			default: return IM_COL32(214, 218, 226, 255);
			}
		}

		void PushUndo(View& v, double now, bool force)
		{
			const std::string cur = v.synced;
			if (!force && now - v.lastEditTime < 0.7 && !v.undo.empty()) { v.lastEditTime = now; return; }
			if (v.undo.empty() || v.undo.back() != cur)
			{
				v.undo.push_back(cur);
				if (v.undo.size() > 300) v.undo.erase(v.undo.begin());
			}
			v.redo.clear();
			v.lastEditTime = now;
		}

		void UpdateCompletion(Editor& ed, const CodeDoc& doc, View& v)
		{
			v.compOpen = false;
			const std::string& line = v.lines[v.cl];
			int s = v.cc;
			while (s > 0 && IsWord(line[s - 1])) --s;
			const std::string prefix = line.substr(s, v.cc - s);
			std::string qualifier;
			if (s > 0 && (line[s - 1] == '.' || line[s - 1] == ':'))
			{
				int q = s - 1;
				int qe = q;
				while (q > 0 && (IsWord(line[q - 1]) || line[q - 1] == '.')) --q;
				qualifier = line.substr(q, qe - q);
			}
			if (prefix.empty() && qualifier.empty()) return;
			if (doc.lua && ed.lsp.Ready())
			{
				const std::string abs = ed.AbsPath(doc.path);
				ed.lsp.Sync(abs, Join(v));
				v.comps = ed.lsp.Complete(abs, v.cl, v.cc);
				if (v.comps.size() > 80) v.comps.resize(80);
			}
			else
				v.comps = LuaComplete(Join(v), prefix, qualifier);
			// don't offer exactly what is typed
			v.comps.erase(std::remove_if(v.comps.begin(), v.comps.end(), [&](const LuaCompletion& c) { return c.label == prefix; }), v.comps.end());
			if (v.comps.empty()) return;
			v.compOpen = true;
			v.compSel = 0;
			v.compStart = s;
		}

		void ToggleComment(View& v, const std::string& mark)
		{
			if (mark.empty()) return;
			int l0, c0, l1, c1;
			OrderSel(v, l0, c0, l1, c1);
			bool all = true;
			for (int l = l0; l <= l1; ++l)
			{
				const size_t p = v.lines[l].find_first_not_of(" \t");
				if (p != std::string::npos && v.lines[l].compare(p, mark.size(), mark) != 0) all = false;
			}
			for (int l = l0; l <= l1; ++l)
			{
				std::string& s = v.lines[l];
				const size_t p = s.find_first_not_of(" \t");
				if (p == std::string::npos) continue;
				if (all) { s.erase(p, s.compare(p, mark.size() + 1, mark + " ") == 0 ? mark.size() + 1 : mark.size()); }
				else s.insert(p, mark + " ");
			}
		}
	}

	void DrawCodeEditor(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::Begin("Code", &ed.panel.code, ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::End();
			return;
		}
		if (ed.docs.empty())
		{
			ImGui::Dummy(ImVec2(0, 40));
			const char* t = "Open a script from the Assets panel";
			ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(t).x) * 0.5f);
			ImGui::TextDisabled("%s", t);
			ImGui::End();
			return;
		}
		// tabs
		int closeIdx = -1;
		if (ImGui::BeginTabBar("##doctabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
		{
			for (size_t i = 0; i < ed.docs.size(); ++i)
			{
				CodeDoc& d = ed.docs[i];
				bool open = true;
				const std::string label = fs::FileName(d.path) + (d.dirty() ? " *" : "") + "###" + d.path;
				ImGuiTabItemFlags fl = (ed.activeDoc == (int)i && d.goToLine != -2) ? 0 : 0;
				if (ImGui::BeginTabItem(label.c_str(), &open, fl))
				{
					ed.activeDoc = (int)i;
					ImGui::EndTabItem();
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.path.c_str());
				if (!open) closeIdx = (int)i;
			}
			ImGui::EndTabBar();
		}
		if (closeIdx >= 0)
		{
			Views().erase(ed.docs[closeIdx].path);
			ed.CloseFile(closeIdx);
			if (ed.docs.empty()) { ImGui::End(); return; }
		}
		if (ed.activeDoc < 0 || ed.activeDoc >= (int)ed.docs.size()) ed.activeDoc = 0;
		CodeDoc& doc = ed.docs[ed.activeDoc];
		View& v = Views()[doc.path];
		ImGuiIO& io = ImGui::GetIO();

		if (doc.text != v.synced)
		{
			// changed from outside (reload, other panel): rebuild
			Split(v, doc.text);
			v.synced = doc.text;
			v.cl = std::min(v.cl, (int)v.lines.size() - 1);
			v.cc = std::min(v.cc, (int)v.lines[v.cl].size());
			v.al = v.cl; v.ac = v.cc;
			doc.diagsStale = true;
		}
		if (doc.goToLine >= 0)
		{
			v.cl = v.al = std::min(doc.goToLine - 1, (int)v.lines.size() - 1);
			v.cc = v.ac = 0;
			v.scrollToCursor = true;
			doc.goToLine = -1;
		}

		// toolbar
		if (ImGui::Button(ICON_MD_SAVE " Save")) { Status s = ed.SaveFile(doc); if (!s) ed.Toast(s.message, true); }
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_SEARCH " Find")) v.findOpen = !v.findOpen;
		ImGui::SameLine();
		ImGui::TextDisabled("%s   Ln %d, Col %d", doc.path.c_str(), v.cl + 1, v.cc + 1);
		if (!doc.diags.empty())
		{
			ImGui::SameLine();
			ImGui::TextColored(ui::col::Bad(), "  %zu problem(s)", doc.diags.size());
		}
		if (v.findOpen)
		{
			ImGui::SetNextItemWidth(180);
			const bool ent = ImGui::InputTextWithHint("##find", "find", v.findText, sizeof(v.findText), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(180);
			ImGui::InputTextWithHint("##repl", "replace with", v.replaceText, sizeof(v.replaceText));
			ImGui::SameLine();
			bool next = ent || ImGui::Button("Next");
			ImGui::SameLine();
			const bool replaceAll = ImGui::Button("Replace all");
			if (next && v.findText[0])
			{
				const std::string needle = v.findText;
				for (int pass = 0, l = v.cl, c = v.cc; pass <= (int)v.lines.size(); ++pass, ++l, c = 0)
				{
					l %= (int)v.lines.size();
					const size_t p = v.lines[l].find(needle, pass == 0 ? c : 0);
					if (p != std::string::npos)
					{
						v.al = l; v.ac = (int)p; v.cl = l; v.cc = (int)(p + needle.size());
						v.scrollToCursor = true;
						break;
					}
				}
			}
			if (replaceAll && v.findText[0])
			{
				PushUndo(v, ImGui::GetTime(), true);
				std::string t = Join(v);
				const std::string a = v.findText, b = v.replaceText;
				size_t p = 0;
				while ((p = t.find(a, p)) != std::string::npos) { t.replace(p, a.size(), b); p += b.size(); }
				doc.text = t;
			}
		}

		// editor surface
		const float lineH = ImGui::GetTextLineHeightWithSpacing();
		const float gutter = 52;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.08f, 0.09f, 1));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 4));
		ImGui::BeginChild("##surface", ImVec2(0, -(doc.diags.empty() ? 0.0f : 54.0f)), false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs);
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		const bool focused = ImGui::IsWindowFocused();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		float maxW = 200;
		for (const std::string& l : v.lines) maxW = std::max(maxW, TextW(l, (int)l.size()));
		ImGui::Dummy(ImVec2(gutter + maxW + 120, lineH * v.lines.size() + 80));
		const bool hovered = ImGui::IsItemHovered();

		if (v.hlDirty)
		{
			CodeHighlight(doc.lang, v.lines, v.spans);
			v.hlDirty = false;
		}

		bool edited = false;
		bool forceUndo = false;
		if (focused)
		{
			io.WantTextInput = true;
			// ---- keyboard ----------------------------------------------------------------------
			const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
			auto moveTo = [&](int l, int c) {
				v.cl = std::max(0, std::min(l, (int)v.lines.size() - 1));
				v.cc = std::max(0, std::min(c, (int)v.lines[v.cl].size()));
				if (!shift) { v.al = v.cl; v.ac = v.cc; }
				v.scrollToCursor = true;
			};
			if (v.compOpen)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { v.compSel = (v.compSel + 1) % (int)v.comps.size(); }
				else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { v.compSel = (v.compSel + (int)v.comps.size() - 1) % (int)v.comps.size(); }
				else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) v.compOpen = false;
			}
			const bool navBlocked = v.compOpen && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow));
			if (!navBlocked)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
				{
					if (!shift && HasSel(v)) { int l0, c0, l1, c1; OrderSel(v, l0, c0, l1, c1); v.cl = v.al = l0; v.cc = v.ac = c0; }
					else if (ctrl) moveTo(v.cl, PrevWord(v.lines[v.cl], v.cc));
					else if (v.cc > 0) moveTo(v.cl, Utf8Prev(v.lines[v.cl], v.cc));
					else if (v.cl > 0) moveTo(v.cl - 1, (int)v.lines[v.cl - 1].size());
					v.compOpen = false;
				}
				if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
				{
					if (!shift && HasSel(v)) { int l0, c0, l1, c1; OrderSel(v, l0, c0, l1, c1); v.cl = v.al = l1; v.cc = v.ac = c1; }
					else if (ctrl) moveTo(v.cl, NextWord(v.lines[v.cl], v.cc));
					else if (v.cc < (int)v.lines[v.cl].size()) moveTo(v.cl, Utf8Next(v.lines[v.cl], v.cc));
					else if (v.cl + 1 < (int)v.lines.size()) moveTo(v.cl + 1, 0);
					v.compOpen = false;
				}
				if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !v.compOpen)
				{
					if (v.wantX < 0) v.wantX = TextW(v.lines[v.cl], v.cc);
					if (v.cl > 0) { const int nl = v.cl - 1; moveTo(nl, ColAtX(v.lines[nl], v.wantX)); }
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !v.compOpen)
				{
					if (v.wantX < 0) v.wantX = TextW(v.lines[v.cl], v.cc);
					if (v.cl + 1 < (int)v.lines.size()) { const int nl = v.cl + 1; moveTo(nl, ColAtX(v.lines[nl], v.wantX)); }
				}
				else if (!ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !ImGui::IsKeyPressed(ImGuiKey_DownArrow) && (io.InputQueueCharacters.Size > 0 || ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow)))
					v.wantX = -1;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Home)) { if (ctrl) moveTo(0, 0); else { const size_t f = v.lines[v.cl].find_first_not_of(" \t"); const int target = (f == std::string::npos || (int)f == v.cc) ? 0 : (int)f; moveTo(v.cl, target); } }
			if (ImGui::IsKeyPressed(ImGuiKey_End)) { if (ctrl) moveTo((int)v.lines.size() - 1, 1 << 20); else moveTo(v.cl, (int)v.lines[v.cl].size()); }
			const int page = std::max(1, (int)(ImGui::GetWindowHeight() / lineH) - 1);
			if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) moveTo(v.cl - page, v.cc);
			if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) moveTo(v.cl + page, v.cc);
			if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A)) { v.al = 0; v.ac = 0; v.cl = (int)v.lines.size() - 1; v.cc = (int)v.lines.back().size(); }
			if (ctrl && ImGui::IsKeyPressed(ImGuiKey_F)) v.findOpen = true;
			if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C) && HasSel(v)) ImGui::SetClipboardText(SelText(v).c_str());
			if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X)) { if (HasSel(v)) { ImGui::SetClipboardText(SelText(v).c_str()); forceUndo = true; DeleteSel(v); edited = true; } }
			if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V)) { if (const char* clip = ImGui::GetClipboardText()) { forceUndo = true; InsertText(v, clip); edited = true; } }
			if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Slash)) { forceUndo = true; ToggleComment(v, LineComment(doc.lang)); edited = true; }
			if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) { doc.text = Join(v); Status s = ed.SaveFile(doc); if (!s) ed.Toast(s.message, true); }
			if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Z))
			{
				if (!v.undo.empty()) { v.redo.push_back(v.synced); doc.text = v.undo.back(); v.undo.pop_back(); }
			}
			if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (shift && ImGui::IsKeyPressed(ImGuiKey_Z))))
			{
				if (!v.redo.empty()) { v.undo.push_back(v.synced); doc.text = v.redo.back(); v.redo.pop_back(); }
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
			{
				if (HasSel(v)) DeleteSel(v);
				else if (v.cc > 0)
				{
					std::string& s = v.lines[v.cl];
					// delete one indent step when only spaces precede the cursor
					int n = Utf8Prev(s, v.cc);
					if (ctrl) n = PrevWord(s, v.cc);
					s.erase(n, v.cc - n);
					v.cc = v.ac = n;
				}
				else if (v.cl > 0)
				{
					const int pc = (int)v.lines[v.cl - 1].size();
					v.lines[v.cl - 1] += v.lines[v.cl];
					v.lines.erase(v.lines.begin() + v.cl);
					--v.cl; v.cc = pc; v.al = v.cl; v.ac = v.cc;
				}
				edited = true;
				v.compOpen = false;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Delete))
			{
				if (HasSel(v)) DeleteSel(v);
				else if (v.cc < (int)v.lines[v.cl].size()) v.lines[v.cl].erase(v.cc, Utf8Next(v.lines[v.cl], v.cc) - v.cc);
				else if (v.cl + 1 < (int)v.lines.size()) { v.lines[v.cl] += v.lines[v.cl + 1]; v.lines.erase(v.lines.begin() + v.cl + 1); }
				edited = true;
			}
			if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && !(v.compOpen && !v.comps.empty()))
			{
				const std::string& cur = v.lines[v.cl];
				const size_t ind = cur.find_first_not_of(' ');
				std::string indent(ind == std::string::npos ? cur.size() : ind, ' ');
				const std::string before = cur.substr(0, v.cc);
				// deeper after block openers
				static const char* openers[] = {"then", "do", "else", "repeat", "{", "("};
				std::string trimmed = before;
				while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
				bool deeper = trimmed.size() >= 8 && trimmed.compare(0, 8, "function") == 0 && false;
				for (const char* o : openers) if (trimmed.size() >= strlen(o) && trimmed.compare(trimmed.size() - strlen(o), strlen(o), o) == 0) deeper = true;
				if (trimmed.find("function") != std::string::npos && trimmed.back() == ')') deeper = true;
				if (deeper) indent += "  ";
				forceUndo = true;
				InsertText(v, "\n" + indent);
				edited = true;
				v.compOpen = false;
			}
			if (v.compOpen && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Tab)) && !v.comps.empty())
			{
				const std::string label = v.comps[v.compSel].label;
				v.lines[v.cl].replace(v.compStart, v.cc - v.compStart, label);
				v.cc = v.ac = v.compStart + (int)label.size();
				v.compOpen = false;
				edited = true;
				forceUndo = true;
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_Tab))
			{
				int l0, c0, l1, c1;
				OrderSel(v, l0, c0, l1, c1);
				forceUndo = true;
				if (l0 != l1)
				{
					for (int l = l0; l <= l1; ++l)
					{
						if (shift) { size_t n = 0; while (n < 2 && n < v.lines[l].size() && v.lines[l][n] == ' ') ++n; v.lines[l].erase(0, n); }
						else v.lines[l].insert(0, "  ");
					}
				}
				else if (shift)
				{
					size_t n = 0; while (n < 2 && n < v.lines[v.cl].size() && v.lines[v.cl][n] == ' ') ++n;
					v.lines[v.cl].erase(0, n);
					v.cc = std::max(0, v.cc - (int)n); v.ac = v.cc;
				}
				else InsertText(v, "  ");
				edited = true;
			}
			// text input
			bool typed = false;
			for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
			{
				const ImWchar c = io.InputQueueCharacters[i];
				if (c < 32 && c != '\t') continue;
				if (c == '\t') continue;
				char buf[5] = {0};
				ImTextCharToUtf8(buf, c);
				InsertText(v, buf);
				// auto close pairs
				if (c == '(' || c == '[' || c == '{' || c == '"' || c == '\'')
				{
					const char close = c == '(' ? ')' : c == '[' ? ']' : c == '{' ? '}' : (char)c;
					const std::string& s = v.lines[v.cl];
					const bool nextIsWord = v.cc < (int)s.size() && IsWord(s[v.cc]);
					if (!nextIsWord) { v.lines[v.cl].insert(v.cc, 1, close); }
				}
				else if ((c == ')' || c == ']' || c == '}') && v.cc < (int)v.lines[v.cl].size() && v.lines[v.cl][v.cc] == (char)c && v.cc >= 2 && v.lines[v.cl][v.cc - 1] == (char)c)
				{
					v.lines[v.cl].erase(v.cc, 1); // typed over the auto inserted closer
				}
				typed = true;
				edited = true;
			}
			io.InputQueueCharacters.resize(0);
			if (typed || (edited && ImGui::IsKeyPressed(ImGuiKey_Backspace)))
				UpdateCompletion(ed, doc, v);
		}

		// ---- mouse ---------------------------------------------------------------------------------
		const ImVec2 mouse = io.MousePos;
		auto posToLC = [&](ImVec2 p, int& l, int& c) {
			l = std::max(0, std::min((int)((p.y - origin.y) / lineH), (int)v.lines.size() - 1));
			c = ColAtX(v.lines[l], p.x - origin.x - gutter);
		};
		if (hovered && ImGui::IsMouseClicked(0) && doc.lua && mouse.x < origin.x + 24)
		{
			int l, c;
			posToLC(mouse, l, c);
			ed.ToggleBreakpoint(doc.path, l + 1); // gutter click: breakpoint
		}
		else if (hovered && ImGui::IsMouseClicked(0))
		{
			int l, c;
			posToLC(mouse, l, c);
			v.cl = l; v.cc = c;
			if (!io.KeyShift) { v.al = l; v.ac = c; }
			v.dragging = true;
			v.compOpen = false;
			v.wantX = -1;
			if (ImGui::IsMouseDoubleClicked(0))
			{
				const std::string& s = v.lines[l];
				int a = c, b = c;
				while (a > 0 && IsWord(s[a - 1])) --a;
				while (b < (int)s.size() && IsWord(s[b])) ++b;
				v.ac = a; v.cc = b;
			}
			ImGui::SetWindowFocus();
		}
		if (v.dragging && ImGui::IsMouseDown(0))
		{
			int l, c;
			posToLC(mouse, l, c);
			v.cl = l; v.cc = c;
		}
		else if (!ImGui::IsMouseDown(0)) v.dragging = false;
		if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

		if (edited)
		{
			PushUndo(v, ImGui::GetTime(), forceUndo);
			doc.text = Join(v);
			v.synced = doc.text;
			v.hlDirty = true;
			v.lastEditTime = ImGui::GetTime();
			v.lastCheck = ImGui::GetTime();
			doc.diagsStale = true;
			v.scrollToCursor = true;
		}
		if (doc.lua && doc.diagsStale && ImGui::GetTime() - v.lastCheck > 0.4)
		{
			if (ed.lsp.Ready())
			{
				const std::string abs = ed.AbsPath(doc.path);
				ed.lsp.Sync(abs, doc.text);
				doc.diags = ed.lsp.Diagnostics(abs);
			}
			else
				doc.diags = LuaCheck(doc.text, doc.path);
			doc.diagsStale = false;
			v.lastCheck = ImGui::GetTime();
		}

		// ---- draw -----------------------------------------------------------------------------------
		const bool pausedHere = doc.lua && ed.dbg.paused && !ed.dbg.file.empty() &&
			str::EqualsIgnoreCase(fs::Normalize(ed.AbsPath(doc.path)), ed.dbg.file);
		const float winY = ImGui::GetScrollY();
		const int first = std::max(0, (int)(winY / lineH) - 1);
		const int last = std::min((int)v.lines.size() - 1, first + (int)(ImGui::GetWindowHeight() / lineH) + 2);
		int sl0, sc0, sl1, sc1;
		OrderSel(v, sl0, sc0, sl1, sc1);
		const float visibleX = ImGui::GetWindowPos().x;
		dl->AddRectFilled(ImVec2(visibleX, ImGui::GetWindowPos().y), ImVec2(visibleX + gutter - 6 + ImGui::GetScrollX(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight()), IM_COL32(255, 255, 255, 6));
		for (int l = first; l <= last; ++l)
		{
			const float y = origin.y + l * lineH;
			const float x0 = origin.x + gutter;
			if (l == v.cl && !HasSel(v))
				dl->AddRectFilled(ImVec2(origin.x + gutter - 4, y), ImVec2(origin.x + gutter + maxW + 120, y + lineH), IM_COL32(255, 255, 255, 9));
			if (HasSel(v) && l >= sl0 && l <= sl1)
			{
				const int a = l == sl0 ? sc0 : 0;
				const int b = l == sl1 ? sc1 : (int)v.lines[l].size();
				const float xa = x0 + TextW(v.lines[l], a), xb = x0 + TextW(v.lines[l], b) + (l < sl1 ? 6.0f : 0.0f);
				dl->AddRectFilled(ImVec2(xa, y), ImVec2(xb, y + lineH), IM_COL32(245, 158, 64, 70));
			}
			// find highlights
			if (v.findOpen && v.findText[0])
			{
				const std::string needle = v.findText;
				size_t p = 0;
				while ((p = v.lines[l].find(needle, p)) != std::string::npos)
				{
					dl->AddRectFilled(ImVec2(x0 + TextW(v.lines[l], (int)p), y), ImVec2(x0 + TextW(v.lines[l], (int)(p + needle.size())), y + lineH), IM_COL32(120, 180, 255, 50));
					p += std::max<size_t>(1, needle.size());
				}
			}
			if (doc.lua)
			{
				const bool bp = ed.HasBreakpoint(doc.path, l + 1);
				const ImVec2 cdot(origin.x + 12, y + lineH * 0.5f);
				if (pausedHere && l + 1 == ed.dbg.line)
				{
					dl->AddRectFilled(ImVec2(origin.x + gutter, y), ImVec2(origin.x + gutter + 4000, y + lineH), IM_COL32(245, 200, 64, 46));
					dl->AddTriangleFilled(ImVec2(cdot.x - 5, cdot.y - 5), ImVec2(cdot.x - 5, cdot.y + 5), ImVec2(cdot.x + 5, cdot.y), IM_COL32(245, 200, 64, 255));
				}
				if (bp) dl->AddCircleFilled(cdot, 4.8f, IM_COL32(226, 70, 70, 255));
				else if (hovered && mouse.x < origin.x + 24 && mouse.y >= y && mouse.y < y + lineH) dl->AddCircleFilled(cdot, 4.8f, IM_COL32(226, 70, 70, 90));
			}
			char num[16];
			snprintf(num, sizeof(num), "%d", l + 1);
			const bool err = std::any_of(doc.diags.begin(), doc.diags.end(), [&](const LuaDiag& d) { return d.line == l + 1; });
			dl->AddText(ImVec2(origin.x + gutter - 12 - ImGui::CalcTextSize(num).x, y), err ? IM_COL32(235, 105, 100, 255) : l == v.cl ? IM_COL32(200, 205, 215, 255) : IM_COL32(100, 106, 120, 255), num);
			const std::string& s = v.lines[l];
			float x = x0;
			int pos = 0;
			for (const LuaSpan& sp : v.spans[l])
			{
				if (sp.begin > pos) { dl->AddText(ImVec2(x, y), ColorFor(LuaTok::Text), s.c_str() + pos, s.c_str() + sp.begin); x += TextW(s.substr(pos, sp.begin - pos), sp.begin - pos); }
				dl->AddText(ImVec2(x, y), ColorFor(sp.kind), s.c_str() + sp.begin, s.c_str() + sp.end);
				x += TextW(s.substr(sp.begin, sp.end - sp.begin), sp.end - sp.begin);
				pos = sp.end;
			}
			if (pos < (int)s.size()) dl->AddText(ImVec2(x, y), ColorFor(LuaTok::Text), s.c_str() + pos, s.c_str() + s.size());
			if (err)
			{
				const float ex = x0 + TextW(s, (int)s.size());
				for (float wx = x0; wx < std::max(ex, x0 + 24); wx += 4) dl->AddLine(ImVec2(wx, y + lineH - 2), ImVec2(wx + 2, y + lineH - 4 + ((int)(wx / 4) % 2) * 2), IM_COL32(235, 105, 100, 255));
			}
		}
		// caret
		if (focused && (int)(ImGui::GetTime() * 2) % 2 == 0 + (v.lastEditTime > ImGui::GetTime() - 0.5 ? 0 : 0))
		{
			const float cx = origin.x + gutter + TextW(v.lines[v.cl], v.cc);
			dl->AddLine(ImVec2(cx, origin.y + v.cl * lineH + 1), ImVec2(cx, origin.y + v.cl * lineH + lineH - 2), IM_COL32(245, 158, 64, 255), 1.6f);
		}
		else if (focused && v.lastEditTime > ImGui::GetTime() - 0.5)
		{
			const float cx = origin.x + gutter + TextW(v.lines[v.cl], v.cc);
			dl->AddLine(ImVec2(cx, origin.y + v.cl * lineH + 1), ImVec2(cx, origin.y + v.cl * lineH + lineH - 2), IM_COL32(245, 158, 64, 255), 1.6f);
		}
		if (v.scrollToCursor)
		{
			v.scrollToCursor = false;
			const float cy = v.cl * lineH, cxp = gutter + TextW(v.lines[v.cl], v.cc);
			if (cy < ImGui::GetScrollY()) ImGui::SetScrollY(cy);
			else if (cy + lineH * 2 > ImGui::GetScrollY() + ImGui::GetWindowHeight()) ImGui::SetScrollY(cy + lineH * 2 - ImGui::GetWindowHeight());
			if (cxp < ImGui::GetScrollX()) ImGui::SetScrollX(std::max(0.0f, cxp - 60));
			else if (cxp > ImGui::GetScrollX() + ImGui::GetWindowWidth() - 40) ImGui::SetScrollX(cxp - ImGui::GetWindowWidth() + 80);
		}
		// completion popup
		if (v.compOpen && focused && !v.comps.empty())
		{
			const float px = origin.x + gutter + TextW(v.lines[v.cl], v.compStart);
			const float py = origin.y + (v.cl + 1) * lineH + 2;
			const int shown = std::min<int>(8, (int)v.comps.size());
			float w = 220;
			for (int i = 0; i < shown; ++i) w = std::max(w, ImGui::CalcTextSize((v.comps[i].label + "   " + v.comps[i].detail).c_str()).x + 24);
			dl->AddRectFilled(ImVec2(px, py), ImVec2(px + w, py + shown * lineH + 6), IM_COL32(32, 34, 40, 250), 4);
			dl->AddRect(ImVec2(px, py), ImVec2(px + w, py + shown * lineH + 6), IM_COL32(70, 74, 86, 255), 4);
			const int start = std::max(0, std::min(v.compSel - shown + 1, (int)v.comps.size() - shown));
			for (int i = 0; i < shown; ++i)
			{
				const int idx = start + i;
				const float yy = py + 3 + i * lineH;
				if (idx == v.compSel) dl->AddRectFilled(ImVec2(px + 2, yy), ImVec2(px + w - 2, yy + lineH), IM_COL32(245, 158, 64, 60), 3);
				dl->AddText(ImVec2(px + 8, yy), IM_COL32(230, 232, 238, 255), v.comps[idx].label.c_str());
				const float lw = ImGui::CalcTextSize(v.comps[idx].label.c_str()).x;
				dl->AddText(ImVec2(px + 8 + lw + 12, yy), IM_COL32(120, 126, 140, 255), v.comps[idx].detail.c_str());
			}
			if (!v.comps[v.compSel].doc.empty())
			{
				std::string d = v.comps[v.compSel].doc;
				if (d.size() > 400) d.resize(400);
				const ImVec2 ts = ImGui::CalcTextSize(d.c_str(), nullptr, false, 300);
				const float dx = px + w + 6;
				dl->AddRectFilled(ImVec2(dx, py), ImVec2(dx + ts.x + 16, py + ts.y + 12), IM_COL32(32, 34, 40, 250), 4);
				dl->AddRect(ImVec2(dx, py), ImVec2(dx + ts.x + 16, py + ts.y + 12), IM_COL32(70, 74, 86, 255), 4);
				dl->AddText(nullptr, 0, ImVec2(dx + 8, py + 6), IM_COL32(190, 194, 205, 255), d.c_str(), nullptr, 300);
			}
		}
		// hover info / go to definition (LSP)
		if (doc.lua && ed.lsp.Ready() && hovered && !v.dragging && !v.compOpen)
		{
			int hl, hc;
			posToLC(mouse, hl, hc);
			const bool overText = mouse.x > origin.x + gutter && hc < (int)v.lines[hl].size();
			if (overText && (ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsMouseClicked(0)))
			{
				const std::string abs = ed.AbsPath(doc.path);
				ed.lsp.Sync(abs, doc.text);
				const EditorLsp::Location loc = ed.lsp.Definition(abs, hl, hc);
				if (loc.found)
				{
					std::string rel = loc.path;
					const std::string root = fs::Normalize(ed.project.Root());
					if (rel.rfind(root, 0) == 0) rel = rel.substr(root.size() + (rel.size() > root.size() && rel[root.size()] == '/' ? 1 : 0));
					if (rel == doc.path) { v.cl = v.al = loc.line; v.cc = v.ac = loc.column; v.scrollToCursor = true; }
					else { ed.OpenFile(rel); if (ed.activeDoc >= 0 && ed.activeDoc < (int)ed.docs.size()) ed.docs[ed.activeDoc].goToLine = loc.line + 1; }
				}
			}
			else if (overText && IsWord(v.lines[hl][hc]) && io.MouseDelta.x == 0 && io.MouseDelta.y == 0)
			{
				static double still = 0; static int lastL = -1, lastC = -1; static std::string cached;
				if (hl != lastL || hc != lastC) { still = ImGui::GetTime(); lastL = hl; lastC = hc; cached.clear(); }
				if (ImGui::GetTime() - still > 0.45)
				{
					if (cached.empty())
					{
						const std::string abs = ed.AbsPath(doc.path);
						ed.lsp.Sync(abs, doc.text);
						cached = ed.lsp.Hover(abs, hl, hc);
						if (cached.empty()) cached = " ";
						// strip markdown code fences
						std::string o; for (const std::string& ln : str::SplitLines(cached)) if (ln.rfind("```", 0) != 0) o += ln + "\n";
						while (!o.empty() && o.back() == '\n') o.pop_back();
						cached = o.empty() ? " " : o;
					}
					if (cached != " ") { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(520); ImGui::TextUnformatted(cached.c_str()); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
				}
			}
			else if (!overText) { /* nothing */ }
		}
		ImGui::EndChild();

		if (!doc.diags.empty())
		{
			ImGui::BeginChild("##diags", ImVec2(0, 50), true);
			for (const LuaDiag& d : doc.diags)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, d.error ? ui::col::Bad() : ui::col::Warn());
				const std::string txt = std::string(ICON_MD_ERROR) + "  " + doc.path + ":" + std::to_string(d.line) + "  " + d.message;
				if (ImGui::Selectable(txt.c_str())) { v.cl = v.al = std::max(0, d.line - 1); v.cc = v.ac = 0; v.scrollToCursor = true; }
				ImGui::PopStyleColor();
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}
}
