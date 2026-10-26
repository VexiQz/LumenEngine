# ---------------------------------------------------------------------------
# Lumen Engine — build options
# ---------------------------------------------------------------------------

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

option(LUMEN_BUILD_ENGINE  "Build lumen_engine (Wicked Engine integration)" ON)
option(LUMEN_BUILD_EDITOR  "Build the Lumen editor (ImGui)" ON)
option(LUMEN_BUILD_PLAYER  "Build the standalone Lumen player" ON)
option(LUMEN_BUILD_TOOLS   "Build lumen-cli" ON)
option(LUMEN_BUILD_TESTS   "Build unit tests" ON)

option(LUMEN_ENABLE_GLSLANG "Compile GLSL (vert/frag/geom/tesc/tese/comp/mesh/task) through glslang" ON)
option(LUMEN_EMBED_RUNTIME  "Embed runtime_data into the binaries (first launch creates LumenRuntime/ from them)" ON)
option(LUMEN_ENABLE_IPO     "Enable link time optimisation in Release" OFF)

set(LUMEN_WICKED_REPOSITORY "https://github.com/turanszkij/WickedEngine.git" CACHE STRING "Wicked Engine git repository")
# Pinned commit of Wicked Engine that this version of Lumen is written against.
set(LUMEN_WICKED_TAG "4f93f3fd4f337aeb38dbfe410701b83024ccdd42" CACHE STRING "Wicked Engine git tag/commit")
set(LUMEN_GLSLANG_TAG "15.4.0" CACHE STRING "glslang git tag")

set(LUMEN_VERSION_MAJOR 1)
set(LUMEN_VERSION_MINOR 9)
set(LUMEN_VERSION_PATCH 2)
set(LUMEN_VERSION "${LUMEN_VERSION_MAJOR}.${LUMEN_VERSION_MINOR}.${LUMEN_VERSION_PATCH}")
