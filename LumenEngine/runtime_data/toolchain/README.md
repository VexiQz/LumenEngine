# Toolchain

Build tools the engine downloads on demand (Build panel > Download missing tools) are installed here:

* `cmake/`, `ninja/` - the build system used for native modules
* `llvm-mingw/` - C++ compiler for Windows when no Visual Studio / MinGW is found
* `downloads/` - archive cache (checksum verified)

Tools found on the machine (Visual Studio, GCC / Clang, CMake, Ninja) are used first; see `config/toolchain.json` for what was detected.

## Managed-only policy

Lumen always builds with its own tools in this folder (cmake, ninja, LLVM-MinGW on Windows). System-installed
tools and Visual Studio are ignored, so a project builds the same on every machine and users install nothing.
Missing tools are downloaded automatically on first launch and before a native build
(`config/runtime.json` → `toolchain.autoDownload`, default `true`). To opt back into system tools set
`toolchain.useSystemTools: true`. On Linux/macOS there is no managed compiler recipe yet: cmake and ninja are
downloaded, the compiler comes from the system (`build-essential` / Xcode CLT).
