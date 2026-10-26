# ---------------------------------------------------------------------------
# Lumen Engine — third party dependencies
#
#   Wicked Engine (MIT)   rendering / scene / Lua 5.4 / audio / video / physics (Jolt)
#   glslang (BSD-3)       GLSL -> SPIR-V (optional)
#   Dear ImGui (MIT)      editor UI; the docking build vendored by Wicked is used
#
# Everything is downloaded at configure time. For offline builds point
#   -DFETCHCONTENT_SOURCE_DIR_WICKEDENGINE=<path>   and
#   -DFETCHCONTENT_SOURCE_DIR_GLSLANG=<path>
# at local checkouts.
# ---------------------------------------------------------------------------

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- Wicked Engine ----------------------------------------------------------
# Wicked's own CMake project is consumed as a subdirectory. We only want the
# engine library, so every optional product is disabled.
set(WICKED_EDITOR          OFF CACHE BOOL "" FORCE)
set(WICKED_TESTS           OFF CACHE BOOL "" FORCE)
set(WICKED_IMGUI_EXAMPLE   OFF CACHE BOOL "" FORCE)
set(WICKED_LINUX_TEMPLATE  OFF CACHE BOOL "" FORCE)
set(WICKED_WINDOWS_TEMPLATE OFF CACHE BOOL "" FORCE)
set(WICKED_ENABLE_SYMLINKS OFF CACHE BOOL "" FORCE)
set(WICKED_ENABLE_IPO ${LUMEN_ENABLE_IPO} CACHE BOOL "" FORCE)

FetchContent_Declare(WickedEngine
    GIT_REPOSITORY ${LUMEN_WICKED_REPOSITORY}
    GIT_TAG        ${LUMEN_WICKED_TAG}
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(WickedEngine)
set(LUMEN_WICKED_DIR "${wickedengine_SOURCE_DIR}" CACHE INTERNAL "")
message(STATUS "Lumen: Wicked Engine source: ${LUMEN_WICKED_DIR}")

# Wicked's Lua (5.4.x) is the one and only Lua of the whole project: the engine,
# the LSP, the bytecode compiler and the command line tools all link to it, so
# bytecode produced by lumen-cli is always loadable by the runtime.
if(NOT TARGET LUA)
    message(FATAL_ERROR "Wicked Engine did not provide the LUA target")
endif()
set(LUMEN_LUA_INCLUDE_DIR "${LUMEN_WICKED_DIR}/WickedEngine/LUA" CACHE INTERNAL "")

# --- glslang ----------------------------------------------------------------
if(LUMEN_ENABLE_GLSLANG)
    set(ENABLE_OPT              OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL             OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_JS       OFF CACHE BOOL "" FORCE)
    set(ENABLE_CTEST            OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS           OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL  OFF CACHE BOOL "" FORCE)
    set(BUILD_EXTERNAL          OFF CACHE BOOL "" FORCE)
    set(SKIP_GLSLANG_INSTALL    ON  CACHE BOOL "" FORCE)
    FetchContent_Declare(glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG        ${LUMEN_GLSLANG_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(glslang)
    set(LUMEN_GLSLANG_DIR "${glslang_SOURCE_DIR}" CACHE INTERNAL "")
endif()
