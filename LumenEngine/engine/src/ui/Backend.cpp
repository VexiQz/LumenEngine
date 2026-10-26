#include "ui/Backend.h"

#include "imgui_internal.h"
#include "IconsMaterialDesign.h"
#ifdef _WIN32
#include "imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#else
#include "imgui_impl_sdl.h"
#endif
#include "lumen/core/Fs.h"

using namespace wi::graphics;

namespace lumen::ui
{
	namespace
	{
		Shader g_vs, g_ps;
		Texture g_fontTex;
		Sampler g_sampler;
		InputLayout g_layout;
		PipelineState g_pso;
		ImFont* g_mono = nullptr;
		ImFont* g_bold = nullptr;
		std::string g_ini;
		bool g_ready = false;
	}

	ImFont* BoldFont() { return g_bold ? g_bold : ImGui::GetFont(); }
	ImFont* MonoFont() { return g_mono ? g_mono : ImGui::GetFont(); }

	static void LoadFonts(const std::string& dir, float scale)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();
		static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0100, 0x024F, 0x0400, 0x052F, 0x2010, 0x2027, 0x2190, 0x21FF, 0x2200, 0x22FF, 0};
		const std::string roboto = dir + "/Roboto-Medium.ttf";
		const std::string icons = dir + "/MaterialIcons-Regular.ttf";
		const float size = 15.0f * scale;
		ImFont* main = lumen::fs::Exists(roboto) ? io.Fonts->AddFontFromFileTTF(roboto.c_str(), size, nullptr, ranges) : nullptr;
		if (!main)
			main = io.Fonts->AddFontDefault();
		if (lumen::fs::Exists(icons))
		{
			static const ImWchar iconRanges[] = {ICON_MIN_MD, ICON_MAX_MD, 0};
			ImFontConfig cfg;
			cfg.MergeMode = true;
			cfg.PixelSnapH = true;
			cfg.GlyphOffset = ImVec2(0, size * 0.22f);
			cfg.GlyphMinAdvanceX = size * 1.1f;
			io.Fonts->AddFontFromFileTTF(icons.c_str(), size * 1.15f, &cfg, iconRanges);
		}
		// Larger variant for headings (same face), and the editor font.
		ImFontConfig big;
		g_bold = lumen::fs::Exists(roboto) ? io.Fonts->AddFontFromFileTTF(roboto.c_str(), size * 1.35f, &big, ranges) : main;
		g_mono = main;
		io.FontDefault = main;
	}

	static bool CreateDeviceObjects()
	{
		ImGuiIO& io = ImGui::GetIO();
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		TextureDesc td;
		td.width = width;
		td.height = height;
		td.mip_levels = 1;
		td.array_size = 1;
		td.format = Format::R8G8B8A8_UNORM;
		td.bind_flags = BindFlag::SHADER_RESOURCE;
		SubresourceData sd;
		sd.data_ptr = pixels;
		sd.row_pitch = width * GetFormatStride(td.format);
		sd.slice_pitch = sd.row_pitch * height;
		GraphicsDevice* dev = GetDevice();
		dev->CreateTexture(&td, &sd, &g_fontTex);
		SamplerDesc sampler;
		sampler.address_u = sampler.address_v = sampler.address_w = TextureAddressMode::CLAMP;
		sampler.filter = Filter::MIN_MAG_MIP_LINEAR;
		dev->CreateSampler(&sampler, &g_sampler);
		io.Fonts->SetTexID((ImTextureID)&g_fontTex);

		g_layout.elements = {
			{"POSITION", 0, Format::R32G32_FLOAT, 0, (uint32_t)IM_OFFSETOF(ImDrawVert, pos), InputClassification::PER_VERTEX_DATA},
			{"TEXCOORD", 0, Format::R32G32_FLOAT, 0, (uint32_t)IM_OFFSETOF(ImDrawVert, uv), InputClassification::PER_VERTEX_DATA},
			{"COLOR", 0, Format::R8G8B8A8_UNORM, 0, (uint32_t)IM_OFFSETOF(ImDrawVert, col), InputClassification::PER_VERTEX_DATA},
		};
		PipelineStateDesc desc;
		desc.vs = &g_vs;
		desc.ps = &g_ps;
		desc.il = &g_layout;
		desc.dss = wi::renderer::GetDepthStencilState(wi::enums::DSSTYPE_DEPTHREAD);
		desc.rs = wi::renderer::GetRasterizerState(wi::enums::RSTYPE_DOUBLESIDED);
		desc.bs = wi::renderer::GetBlendState(wi::enums::BSTYPE_TRANSPARENT);
		desc.pt = PrimitiveTopology::TRIANGLELIST;
		return dev->CreatePipelineState(&desc, &g_pso);
	}

	bool Init(wi::platform::window_type window, const BackendConfig& config)
	{
		{
			const std::string prev = wi::renderer::GetShaderSourcePath();
			wi::renderer::SetShaderSourcePath(config.shaderDir + "/");
			wi::renderer::LoadShader(ShaderStage::VS, g_vs, "ImGuiVS.cso");
			wi::renderer::LoadShader(ShaderStage::PS, g_ps, "ImGuiPS.cso");
			wi::renderer::SetShaderSourcePath(prev);
		}
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigWindowsMoveFromTitleBarOnly = true;
		io.ConfigDockingWithShift = false;
		g_ini = config.iniFile;
		io.IniFilename = g_ini.empty() ? nullptr : g_ini.c_str();
		ApplyTheme();
		LoadFonts(config.fontDir, 1.0f);
#ifdef _WIN32
		ImGui_ImplWin32_Init(window);
#else
		ImGui_ImplSDL2_InitForVulkan(window);
#endif
		io.BackendRendererName = "Lumen/Wicked";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
		g_ready = CreateDeviceObjects();
		return g_ready;
	}

	void Shutdown()
	{
		if (!ImGui::GetCurrentContext())
			return;
#ifdef _WIN32
		ImGui_ImplWin32_Shutdown();
#else
		ImGui_ImplSDL2_Shutdown();
#endif
		ImGui::DestroyContext();
		g_ready = false;
	}

	void NewFrame()
	{
#ifdef _WIN32
		ImGui_ImplWin32_NewFrame();
#else
		ImGui_ImplSDL2_NewFrame();
#endif
		ImGui::NewFrame();
	}

	void ProcessEvent(const void* e)
	{
#ifndef _WIN32
		ImGui_ImplSDL2_ProcessEvent((const SDL_Event*)e);
#else
		(void)e;
#endif
	}

	void Draw(CommandList cmd)
	{
		ImGui::Render();
		ImDrawData* dd = ImGui::GetDrawData();
		if (!g_ready || !dd || dd->TotalVtxCount == 0)
			return;
		const int fbw = (int)(dd->DisplaySize.x * dd->FramebufferScale.x);
		const int fbh = (int)(dd->DisplaySize.y * dd->FramebufferScale.y);
		if (fbw <= 0 || fbh <= 0)
			return;
		GraphicsDevice* device = GetDevice();
		auto vb = device->AllocateGPU(sizeof(ImDrawVert) * dd->TotalVtxCount, cmd);
		auto ib = device->AllocateGPU(sizeof(ImDrawIdx) * dd->TotalIdxCount, cmd);
		ImDrawVert* v = reinterpret_cast<ImDrawVert*>(vb.data);
		ImDrawIdx* ix = reinterpret_cast<ImDrawIdx*>(ib.data);
		for (int n = 0; n < dd->CmdListsCount; ++n)
		{
			const ImDrawList* l = dd->CmdLists[n];
			memcpy(v, l->VtxBuffer.Data, l->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(ix, l->IdxBuffer.Data, l->IdxBuffer.Size * sizeof(ImDrawIdx));
			v += l->VtxBuffer.Size;
			ix += l->IdxBuffer.Size;
		}
		struct Constants { float mvp[4][4]; } c;
		const float L = dd->DisplayPos.x, R = dd->DisplayPos.x + dd->DisplaySize.x;
		const float T = dd->DisplayPos.y, B = dd->DisplayPos.y + dd->DisplaySize.y;
		const float mvp[4][4] = {
			{2.0f / (R - L), 0, 0, 0},
			{0, 2.0f / (T - B), 0, 0},
			{0, 0, 0.5f, 0},
			{(R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f},
		};
		memcpy(c.mvp, mvp, sizeof(mvp));
		device->BindDynamicConstantBuffer(c, 0, cmd);
		const GPUBuffer* vbs[] = {&vb.buffer};
		const uint32_t strides[] = {sizeof(ImDrawVert)};
		const uint64_t offsets[] = {vb.offset};
		device->BindVertexBuffers(vbs, 0, 1, strides, offsets, cmd);
		device->BindIndexBuffer(&ib.buffer, IndexBufferFormat::UINT16, ib.offset, cmd);
		Viewport vp;
		vp.width = (float)fbw;
		vp.height = (float)fbh;
		device->BindViewports(1, &vp, cmd);
		device->BindPipelineState(&g_pso, cmd);
		device->BindSampler(&g_sampler, 0, cmd);
		const ImVec2 off = dd->DisplayPos, scale = dd->FramebufferScale;
		int32_t vtxOffset = 0;
		uint32_t idxOffset = 0;
		for (int n = 0; n < dd->CmdListsCount; ++n)
		{
			const ImDrawList* l = dd->CmdLists[n];
			for (const ImDrawCmd& cmdi : l->CmdBuffer)
			{
				if (cmdi.UserCallback)
				{
					if (cmdi.UserCallback != ImDrawCallback_ResetRenderState)
						cmdi.UserCallback(l, &cmdi);
				}
				else
				{
					const ImVec2 mn((cmdi.ClipRect.x - off.x) * scale.x, (cmdi.ClipRect.y - off.y) * scale.y);
					const ImVec2 mx((cmdi.ClipRect.z - off.x) * scale.x, (cmdi.ClipRect.w - off.y) * scale.y);
					if (mx.x <= mn.x || mx.y <= mn.y)
					{
						idxOffset += cmdi.ElemCount;
						continue;
					}
					Rect scissor;
					scissor.left = (int32_t)mn.x;
					scissor.top = (int32_t)mn.y;
					scissor.right = (int32_t)mx.x;
					scissor.bottom = (int32_t)mx.y;
					device->BindScissorRects(1, &scissor, cmd);
					device->BindResource((const Texture*)cmdi.TextureId, 0, cmd);
					device->DrawIndexed(cmdi.ElemCount, idxOffset, vtxOffset + (int32_t)cmdi.VtxOffset, cmd);
				}
				idxOffset += cmdi.ElemCount;
			}
			vtxOffset += l->VtxBuffer.Size;
		}
		Rect full;
		full.left = 0;
		full.top = 0;
		full.right = fbw;
		full.bottom = fbh;
		device->BindScissorRects(1, &full, cmd);
	}
}
