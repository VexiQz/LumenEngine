// Lumen Engine — builds the project's native module with the (managed) toolchain and loads it into the game.
#pragma once
#include "lumen/core/Packages.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Project.h"
#include "lumen/core/Toolchain.h"
#include <atomic>
#include <functional>

struct lua_State;

namespace lumen
{
	struct NativeBuildOptions
	{
		bool release = false;
		std::string includeDir; // lumen_runtime/include
		PackageEnvironment packages;
		std::function<void(const std::string&)> onOutput;
		std::atomic<bool>* cancel = nullptr;
	};
	struct NativeBuildResult
	{
		Status status;
		std::string library;            // absolute path of the produced shared library
		std::vector<std::string> errors; // compiler diagnostics "file:line: message"
	};
	// Writes <project>/.lumen/native/CMakeLists.txt, configures and builds.
	NativeBuildResult BuildNativeModule(const Project& project, const Toolchain& toolchain, const NativeBuildOptions& options);

	// Runtime side: loads a built module and forwards the engine services to it.
	class NativeModuleHost
	{
	public:
		~NativeModuleHost() { Close(nullptr); }
		Status Open(const std::string& libraryPath, lua_State* L, const void* hostApi);
		void Close(lua_State* L);
		void Update(double dt) { if (update_) update_(dt); }
		void FixedUpdate(double dt) { if (fixed_) fixed_(dt); }
		bool IsOpen() const { return lib_.IsOpen(); }

	private:
		platform::SharedLibrary lib_;
		void (*close_)(lua_State*) = nullptr;
		void (*update_)(double) = nullptr;
		void (*fixed_)(double) = nullptr;
	};
}
