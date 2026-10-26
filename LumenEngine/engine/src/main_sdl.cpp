// Lumen Engine — Linux entry point (SDL2 window, Vulkan).
#include "App.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Runtime.h"
#include "sdl2.h"
#include <thread>

using namespace lumen;

static EditorApp g_app;

static void PrepareRuntime()
{
	Log::InstallConsoleSink(true);
	RuntimeOptions o;
	// Payload: the copy shipped beside the executable first, then the source tree (newer while developing).
	for (const std::string& d : {fs::Join(platform::ExecutableDir(), "runtime_data"), std::string(LUMEN_RUNTIME_DATA_DIR)})
		if (fs::IsDirectory(d))
			o.sourceDirs.push_back(d);
	Status s = Runtime::Initialize(o);
	if (!s)
		LUMEN_ERROR("runtime", "%s", s.message.c_str());
}

static bool g_quit = false;

static void PumpEvents()
{
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_QUIT: g_quit = true; break;
		case SDL_WINDOWEVENT:
			switch (event.window.event)
			{
			case SDL_WINDOWEVENT_CLOSE: g_quit = true; break;
			case SDL_WINDOWEVENT_RESIZED: g_app.SetWindow(g_app.window); break;
			case SDL_WINDOWEVENT_FOCUS_LOST: g_app.is_window_active = false; break;
			case SDL_WINDOWEVENT_FOCUS_GAINED: g_app.is_window_active = true; break;
			default: break;
			}
			break;
		default: break;
		}
		wi::input::sdlinput::ProcessEvent(event);
		ui::ProcessEvent(&event);
	}
	if (g_app.quitRequested)
		g_quit = true;
}

static int Loop()
{
	// also used while the Lua debugger holds the game paused (keeps the window responsive)
	g_app.pumpEvents = PumpEvents;
	while (!g_quit)
	{
		g_app.Run();
		PumpEvents();
	}
	return 0;
}

int main(int argc, char* argv[])
{
	wi::arguments::Parse(argc, argv);
	PrepareRuntime();
	if (argc > 1 && argv[1][0] != '-')
		g_app.bootProject = argv[1];

	sdl2::sdlsystem_ptr_t system = sdl2::make_sdlsystem(SDL_INIT_EVERYTHING | SDL_INIT_EVENTS);
	if (!system)
	{
		LUMEN_ERROR("platform", "SDL2 could not be initialised");
		return 1;
	}
	sdl2::window_ptr_t window = sdl2::make_window("Lumen Engine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 960,
		SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!window)
	{
		LUMEN_ERROR("platform", "window could not be created (Vulkan driver missing?)");
		return 1;
	}
	g_app.SetWindow(window.get());
	const int rc = Loop();
	SDL_Quit();
	return rc;
}
