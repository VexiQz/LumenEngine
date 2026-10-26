// Lumen Engine — Windows entry point (Win32 window, Vulkan or DX12 through Wicked).
#include "App.h"
#include "Diagnostics.h"
#include "wiBacklog.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Runtime.h"
#include <shellapi.h>

using namespace lumen;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static EditorApp g_app;
static bool g_graphicsInitialized = false;

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
		return true;

	if (message == WM_CLOSE || message == WM_DESTROY || message == WM_QUIT)
		LUMEN_INFO("window", "WM message=%u hwnd=0x%p", message, hWnd);

	switch (message)
	{
	case WM_SIZE:
	case WM_DPICHANGED:
		if (g_graphicsInitialized && g_app.is_window_active)
		{
			diagnostics::Stage(message == WM_SIZE ? "WM_SIZE -> SetWindow" : "WM_DPICHANGED -> SetWindow");
			LUMEN_INFO("window", "WM message=%u; client=%ux%u; hwnd=0x%p", message,
				(unsigned int)LOWORD(lParam), (unsigned int)HIWORD(lParam), hWnd);
			g_app.SetWindow(hWnd);
			LUMEN_INFO("window", "SetWindow returned for WM message=%u", message);
		}
		break;
	case WM_SETFOCUS:
		g_app.is_window_active = true;
		LUMEN_INFO("window", "WM_SETFOCUS");
		break;
	case WM_KILLFOCUS:
		g_app.is_window_active = false;
		LUMEN_INFO("window", "WM_KILLFOCUS");
		break;
	case WM_CLOSE:
		LUMEN_INFO("window", "WM_CLOSE -> PostQuitMessage");
		PostQuitMessage(0);
		return 0;
	case WM_DESTROY:
		LUMEN_INFO("window", "WM_DESTROY -> PostQuitMessage");
		PostQuitMessage(0);
		return 0;
	default: break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	Log::InstallConsoleSink(false);
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	std::vector<std::string> engineArgs;
	engineArgs.reserve((size_t)std::max(argc, 1) + 1);
	engineArgs.emplace_back("lumen");
	bool backendSpecified = false;
	for (int i = 1; argv && i < argc; ++i)
	{
		std::string arg = platform::WideToUtf8(argv[i]);
		while (!arg.empty() && arg[0] == '-')
			arg.erase(arg.begin());
		if (arg == "dx12" || arg == "vulkan")
			backendSpecified = true;
		engineArgs.push_back(std::move(arg));
	}
	if (!backendSpecified)
	{
		engineArgs.emplace_back("dx12");
		LUMEN_INFO("startup", "No graphics backend selected; defaulting to DX12 to avoid the Intel Vulkan driver crash");
	}
	std::vector<char*> engineArgv;
	engineArgv.reserve(engineArgs.size());
	for (std::string& arg : engineArgs)
		engineArgv.push_back(arg.data());
	wi::arguments::Parse((int)engineArgv.size(), engineArgv.data());
	const std::string logsDir = fs::Join(platform::ExecutableDir(), "lumen_runtime", "logs");
	diagnostics::Install(logsDir);
	diagnostics::Stage("Runtime::Initialize");
	RuntimeOptions o;
	// Payload: the copy shipped beside the executable first, then the source tree (newer while developing).
	for (const std::string& d : {fs::Join(platform::ExecutableDir(), "runtime_data"), std::string(LUMEN_RUNTIME_DATA_DIR)})
		if (fs::IsDirectory(d))
			o.sourceDirs.push_back(d);
	Status s = Runtime::Initialize(o);
	if (!s)
	{
		LUMEN_ERROR("runtime", "%s", s.message.c_str());
		diagnostics::Stage("Runtime::Initialize failed");
	}
	else
	{
		wi::backlog::SetLogFile(fs::Join(Runtime::Layout().logs, "wicked.log"));
		LUMEN_INFO("startup", "Runtime ready at %s", Runtime::Layout().root.c_str());
	}

	std::string boot_log_path = fs::Join(logsDir, "window_boot.log");
	Log::InstallFileSink(boot_log_path, 1ull << 20, 3);
	LUMEN_INFO("window", "wWinMain start: executable=%s", platform::ExecutableDir().c_str());

	for (int i = 1; argv && i < argc; ++i)
	{
		std::string arg = platform::WideToUtf8(argv[i]);
		const bool option = !arg.empty() && arg[0] == '-';
		while (!arg.empty() && arg[0] == '-')
			arg.erase(arg.begin());
		if (option || arg == "dx12" || arg == "vulkan" || arg == "igpu" || arg == "nvidiagpu" ||
			arg == "amdgpu" || arg == "intelgpu" || arg == "debugdevice" || arg == "gpuvalidation" ||
			arg == "gpu_verbose" || arg == "alwaysactive")
			continue;
		g_app.bootProject = std::move(arg);
		break;
	}
	if (argv)
		LocalFree(argv);

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = L"LumenEngineWindow";
	diagnostics::Stage("RegisterClassExW");
	LUMEN_INFO("window", "RegisterClassExW");
	if (!RegisterClassExW(&wc))
		LUMEN_WARN("window", "RegisterClassExW failed or class already exists: code=%lu", GetLastError());
	diagnostics::Stage("CreateWindowW");
	LUMEN_INFO("window", "CreateWindowW begin");
	HWND hWnd = CreateWindowW(L"LumenEngineWindow", L"Lumen Engine", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 1600, 960, nullptr, nullptr, hInstance, nullptr);
	if (!hWnd)
	{
		LUMEN_ERROR("window", "CreateWindowW failed with code=%lu", GetLastError());
		diagnostics::Stop();
		return 1;
	}
	LUMEN_INFO("window", "CreateWindowW OK hwnd=0x%p", hWnd);
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	LUMEN_INFO("window", "ShowWindow + UpdateWindow done");
	diagnostics::Stage("main SetWindow");
	g_app.SetWindow(hWnd);
	g_graphicsInitialized = true;
	LUMEN_INFO("window", "main SetWindow returned");

	// While the Lua debugger pauses the game the hook keeps the window alive through this pump.
	g_app.pumpEvents = [] {
		MSG m = {};
		while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE))
		{
			if (m.message == WM_QUIT) { g_app.quitRequested = true; PostQuitMessage((int)m.wParam); break; }
			TranslateMessage(&m);
			DispatchMessage(&m);
		}
	};
	MSG msg = {};
	LUMEN_INFO("window", "entering message loop");
	diagnostics::Stage("message loop");
	uint64_t frame = 0;
	while (msg.message != WM_QUIT && !g_app.quitRequested)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			if (frame == 0)
				diagnostics::Stage("first EditorApp::Run");
			g_app.Run();
			diagnostics::FrameCompleted(++frame);
		}
	}
	LUMEN_INFO("window", "exit message loop: quitRequested=%d msg=%u wparam=%llu", g_app.quitRequested, msg.message, (unsigned long long)msg.wParam);
	Log::Flush();
	diagnostics::Stop();
	return (int)msg.wParam;
}
