#include "Panels.h"

#include <algorithm>
#include <filesystem>

namespace lumen
{
	namespace
	{
		struct Browser
		{
			std::string dir;
			bool pickFile = false; // true: choose a .lum file, false: choose a folder
		};

		std::string HomeDir()
		{
#ifdef _WIN32
			const char* h = getenv("USERPROFILE");
#else
			const char* h = getenv("HOME");
#endif
			return h ? fs::Normalize(h) : fs::CurrentDir();
		}

		// Modal file system browser. Returns true when the user confirmed `out`.
		bool BrowserPopup(const char* id, Browser& b, std::string& out)
		{
			bool done = false;
			ImGui::SetNextWindowSize(ImVec2(620, 440), ImGuiCond_Appearing);
			if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoSavedSettings))
			{
				if (b.dir.empty()) b.dir = HomeDir();
				char buf[512];
				snprintf(buf, sizeof(buf), "%s", b.dir.c_str());
				ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##dir", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue) && fs::IsDirectory(buf)) b.dir = fs::Normalize(buf);
				ImGui::BeginChild("entries", ImVec2(0, -40), true);
				if (ImGui::Selectable(ICON_MD_FOLDER "  ..", false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0))
				{
					const std::string p = fs::Parent(b.dir);
					if (!p.empty()) b.dir = p;
				}
				std::vector<std::filesystem::directory_entry> dirs, files;
				std::error_code ec;
				for (const auto& e : std::filesystem::directory_iterator(std::filesystem::u8path(b.dir), std::filesystem::directory_options::skip_permission_denied, ec))
				{
					std::error_code ec2;
					if (e.is_directory(ec2)) dirs.push_back(e);
					else if (b.pickFile && e.path().extension() == ".lum") files.push_back(e);
				}
				auto name = [](const std::filesystem::directory_entry& e) { return fs::FromPath(e.path().filename()); };
				std::sort(dirs.begin(), dirs.end(), [&](auto& a, auto& c) { return str::ToLower(name(a)) < str::ToLower(name(c)); });
				for (const auto& d : dirs)
				{
					const std::string n = name(d);
					if (!n.empty() && n[0] == '.') continue;
					if (ImGui::Selectable((std::string(ICON_MD_FOLDER) + "  " + n).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0))
						b.dir = fs::Join(b.dir, n);
				}
				for (const auto& f : files)
					if (ImGui::Selectable((std::string(ICON_MD_INSERT_DRIVE_FILE) + "  " + name(f)).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0))
					{
						out = fs::Join(b.dir, name(f));
						done = true;
						ImGui::CloseCurrentPopup();
					}
				ImGui::EndChild();
				if (!b.pickFile && ImGui::Button("Select this folder", ImVec2(160, 0)))
				{
					out = b.dir;
					done = true;
					ImGui::CloseCurrentPopup();
				}
				if (b.pickFile)
				{
					// an opened folder that holds a .lum is a project too
					if (ImGui::Button("Open this folder as project", ImVec2(220, 0)))
					{
						out = b.dir;
						done = true;
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}
			return done;
		}
	}

	void DrawHub(Ui& ui)
	{
		Editor& ed = *ui.ed;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("##hub", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImGui::PopStyleVar(2);

		const float leftW = 330;
		ImGui::BeginChild("##left", ImVec2(leftW, 0), false, ImGuiWindowFlags_NoScrollbar);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetWindowPos();
		dl->AddRectFilled(p, ImVec2(p.x + leftW, p.y + ImGui::GetWindowHeight()), IM_COL32(20, 21, 25, 255));
		ImGui::SetCursorPos(ImVec2(32, 56));
		ImGui::PushFont(ui::BoldFont());
		ImGui::PushStyleColor(ImGuiCol_Text, ui::col::Accent());
		ImGui::TextUnformatted("LUMEN");
		ImGui::PopStyleColor();
		ImGui::PopFont();
		ImGui::SetCursorPosX(32);
		ImGui::TextDisabled("Lua + C++ game engine");
		ImGui::SetCursorPos(ImVec2(32, 140));
		static Browser openBrowser{"", true};
		static Browser dirBrowser{"", false};
		static bool newDialog = false;
		if (ImGui::Button(ICON_MD_ADD "   New project", ImVec2(leftW - 64, 38))) newDialog = true;
		ImGui::SetCursorPosX(32);
		if (ImGui::Button(ICON_MD_FOLDER_OPEN "   Open project", ImVec2(leftW - 64, 38))) ImGui::OpenPopup("Open project");
		ImGui::SetCursorPosX(32);
		ImGui::Dummy(ImVec2(0, 16));
		ImGui::SetCursorPosX(32);
		ImGui::TextDisabled("Engine %s", LUMEN_VERSION_STRING);
		ImGui::SetCursorPosX(32);
		ImGui::TextDisabled("Runtime:");
		ImGui::SetCursorPosX(32);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		ImGui::TextDisabled("%s", ed.runtimeRoot.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("##right", ImVec2(0, 0), false);
		ImGui::SetCursorPos(ImVec2(40, 48));
		ImGui::PushFont(ui::BoldFont());
		ImGui::TextUnformatted("Recent projects");
		ImGui::PopFont();
		ImGui::SetCursorPosX(40);
		const std::vector<std::string> recent = ed.Recent();
		if (recent.empty())
		{
			ImGui::SetCursorPosX(40);
			ImGui::TextDisabled("Nothing here yet. Create a project from a template to start.");
		}
		for (const std::string& r : recent)
		{
			ImGui::SetCursorPosX(40);
			ImGui::PushID(r.c_str());
			const std::string name = fs::Stem(r);
			if (ImGui::Selectable(("##" + r).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(ImGui::GetContentRegionAvail().x - 40, 52)))
			{
				if (ImGui::IsMouseDoubleClicked(0))
				{
					Status s = ed.OpenProject(r);
					if (!s) { ed.Toast(s.message, true); ed.RemoveRecent(r); }
				}
			}
			const ImVec2 mn = ImGui::GetItemRectMin();
			ImGui::GetWindowDrawList()->AddText(ImVec2(mn.x + 14, mn.y + 6), IM_COL32(230, 232, 238, 255), name.c_str());
			ImGui::GetWindowDrawList()->AddText(ImVec2(mn.x + 14, mn.y + 28), IM_COL32(120, 126, 140, 255), r.c_str());
			if (ImGui::BeginPopupContextItem("rc"))
			{
				if (ImGui::MenuItem("Open")) { Status s = ed.OpenProject(r); if (!s) ed.Toast(s.message, true); }
				if (ImGui::MenuItem("Remove from list")) ed.RemoveRecent(r);
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		ImGui::EndChild();

		// open dialog
		if (ImGui::BeginPopupModal("##x")) ImGui::EndPopup(); // keeps ids stable when no popup is open
		{
			std::string chosen;
			if (BrowserPopup("Open project", openBrowser, chosen))
			{
				Status s = ed.OpenProject(chosen);
				if (!s) ed.Toast(s.message, true);
			}
		}

		// new project dialog
		if (newDialog)
		{
			ImGui::OpenPopup("New project");
			newDialog = false;
		}
		ImGui::SetNextWindowSize(ImVec2(560, 380), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
		{
			static char name[64] = "MyGame";
			static std::string location;
			static std::string templ = "default";
			static bool native = true;
			if (location.empty()) location = fs::Join(HomeDir(), "LumenProjects");
			ImGui::TextDisabled("Name");
			ImGui::SetNextItemWidth(-1);
			ImGui::InputText("##name", name, sizeof(name));
			ImGui::TextDisabled("Location");
			char loc[512];
			snprintf(loc, sizeof(loc), "%s", location.c_str());
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 40);
			if (ImGui::InputText("##loc", loc, sizeof(loc))) location = loc;
			ImGui::SameLine();
			if (ImGui::Button(ICON_MD_FOLDER_OPEN "##browse")) ImGui::OpenPopup("Choose folder");
			std::string picked;
			if (BrowserPopup("Choose folder", dirBrowser, picked)) location = picked;
			ImGui::TextDisabled("Template");
			std::vector<std::string> templates;
			std::error_code ec;
			for (const auto& e : std::filesystem::directory_iterator(std::filesystem::u8path(fs::Join(ed.runtimeRoot, "templates")), ec))
				if (e.is_directory()) templates.push_back(fs::FromPath(e.path().filename()));
			std::sort(templates.begin(), templates.end());
			if (templates.empty()) templates.push_back("default");
			for (const std::string& t : templates)
			{
				std::string desc;
				std::string text;
				if (fs::ReadText(fs::Join(ed.runtimeRoot, "templates", t, "template.json"), text))
				{
					Json j;
					if (Json::Parse(text, j)) desc = j.GetString("description");
				}
				const bool sel = templ == t;
				if (ImGui::Selectable(("##t" + t).c_str(), sel, 0, ImVec2(0, 44))) templ = t;
				const ImVec2 mn = ImGui::GetItemRectMin();
				ImGui::GetWindowDrawList()->AddText(ImVec2(mn.x + 10, mn.y + 4), IM_COL32(230, 232, 238, 255), t.c_str());
				ImGui::GetWindowDrawList()->AddText(ImVec2(mn.x + 10, mn.y + 23), IM_COL32(120, 126, 140, 255), desc.c_str());
			}
			ImGui::Checkbox("Include a C++ module (native/)", &native);
			ImGui::Separator();
			const std::string safe = fs::SanitizeFileName(name);
			const std::string target = fs::Join(location, safe);
			const bool exists = fs::Exists(target) && !fs::IsEmptyDirectory(target);
			if (exists) ImGui::TextColored(ui::col::Bad(), "%s already exists and is not empty", target.c_str());
			else ImGui::TextDisabled("%s", target.c_str());
			ImGui::BeginDisabled(exists || !name[0]);
			if (ImGui::Button("Create", ImVec2(140, 0)))
			{
				Status s = ed.NewProject(target, name, templ, native);
				if (!s) ed.Toast(s.message, true);
				else ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::End();
	}
}
