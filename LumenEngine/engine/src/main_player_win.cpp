// Lumen Engine — standalone player, Windows entry point.
#include "Player.h"
#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"
#include <shellapi.h>

using namespace lumen;

static PlayerApp g_app;

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_SIZE:
	case WM_DPICHANGED:
		if (g_app.is_window_active)
			g_app.SetWindow(hWnd);
		break;
	case WM_SETFOCUS: g_app.is_window_active = true; break;
	case WM_KILLFOCUS: g_app.is_window_active = false; break;
	case WM_CLOSE:
	case WM_DESTROY: PostQuitMessage(0); return 0;
	default: break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

static std::wstring Widen(const std::string& s)
{
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	std::wstring w((size_t)std::max(0, n - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
	return w;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	wi::arguments::Parse(lpCmdLine);
	Log::InstallConsoleSink(false);
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	std::string arg;
	if (argv && argc > 1 && argv[1][0] != L'-')
	{
		const int n = WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, nullptr, 0, nullptr, nullptr);
		arg.assign((size_t)std::max(0, n - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, arg.data(), n, nullptr, nullptr);
	}
	const Status s = g_app.Prepare(FindPackage(arg));
	if (!s)
	{
		MessageBoxW(nullptr, Widen(s.message).c_str(), L"Cannot start the game", MB_ICONERROR | MB_OK);
		return 1;
	}
	const WindowSettings& w = g_app.GetProject().Settings().window;
	const std::string title = !w.title.empty() ? w.title : g_app.GetProject().Settings().name;

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = L"LumenPlayerWindow";
	RegisterClassExW(&wc);
	DWORD style = w.borderless || w.fullscreen ? WS_POPUP : (w.resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)));
	RECT r = {0, 0, w.width, w.height};
	AdjustWindowRect(&r, style, FALSE);
	HWND hWnd = CreateWindowW(L"LumenPlayerWindow", Widen(title).c_str(), style, CW_USEDEFAULT, 0, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInstance, nullptr);
	if (!hWnd)
		return 1;
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	g_app.SetWindow(hWnd);

	MSG msg = {};
	while (msg.message != WM_QUIT && !g_app.quitRequested)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
			g_app.Run();
	}
	Log::Flush();
	return (int)msg.wParam;
}
