// Lumen Engine — standalone player, Linux entry point (SDL2 + Vulkan).
#include "Player.h"
#include "lumen/core/Log.h"
#include "sdl2.h"

using namespace lumen;

static PlayerApp g_app;

int main(int argc, char* argv[])
{
	wi::arguments::Parse(argc, argv);
	Log::InstallConsoleSink(true);
	const Status s = g_app.Prepare(FindPackage(argc > 1 && argv[1][0] != '-' ? argv[1] : ""));
	if (!s)
	{
		LUMEN_ERROR("player", "%s", s.message.c_str());
		return 1;
	}
	sdl2::sdlsystem_ptr_t system = sdl2::make_sdlsystem(SDL_INIT_EVERYTHING | SDL_INIT_EVENTS);
	if (!system)
		return 1;
	const WindowSettings& w = g_app.GetProject().Settings().window;
	const std::string title = !w.title.empty() ? w.title : g_app.GetProject().Settings().name;
	Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI;
	if (w.resizable) flags |= SDL_WINDOW_RESIZABLE;
	if (w.borderless) flags |= SDL_WINDOW_BORDERLESS;
	sdl2::window_ptr_t window = sdl2::make_window(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w.width, w.height, flags);
	if (!window)
	{
		LUMEN_ERROR("player", "window could not be created (Vulkan driver missing?)");
		return 1;
	}
	g_app.SetWindow(window.get());
	bool quit = false;
	while (!quit && !g_app.quitRequested)
	{
		g_app.Run();
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT: quit = true; break;
			case SDL_WINDOWEVENT:
				switch (event.window.event)
				{
				case SDL_WINDOWEVENT_CLOSE: quit = true; break;
				case SDL_WINDOWEVENT_RESIZED: g_app.SetWindow(g_app.window); break;
				case SDL_WINDOWEVENT_FOCUS_LOST: g_app.is_window_active = false; break;
				case SDL_WINDOWEVENT_FOCUS_GAINED: g_app.is_window_active = true; break;
				default: break;
				}
				break;
			default: break;
			}
			wi::input::sdlinput::ProcessEvent(event);
		}
	}
	Log::Flush();
	SDL_Quit();
	return 0;
}
