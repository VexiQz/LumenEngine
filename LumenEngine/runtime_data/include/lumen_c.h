/* Lumen Engine — C ABI for native game modules.
 *
 * A game module is a shared library built from the project's native/ folder by the engine ("Build" panel or
 * `lumen build-native`). The engine loads it when the game starts and calls lumen_module_open with the game's Lua
 * state; the module registers whatever it wants through the regular Lua C API (lua.h is shipped in include/lua).
 *
 *   #include "lumen_c.h"
 *   LUMEN_EXPORT int lumen_module_open(lua_State* L, const LumenHost* host) {
 *     host->log(LUMEN_LOG_INFO, "hello from native code");
 *     lua_newtable(L); ...; lua_setglobal(L, "native");
 *     return 0;
 *   }
 */
#ifndef LUMEN_C_H
#define LUMEN_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lua/lua.h"
#include "lua/lauxlib.h"

#if defined(_WIN32)
#define LUMEN_EXPORT __declspec(dllexport)
#else
#define LUMEN_EXPORT __attribute__((visibility("default")))
#endif

#define LUMEN_API_VERSION 1

enum { LUMEN_LOG_DEBUG = 0, LUMEN_LOG_INFO = 1, LUMEN_LOG_WARN = 2, LUMEN_LOG_ERROR = 3 };

typedef struct LumenVec3 { float x, y, z; } LumenVec3;

/* Services the engine offers to native code. Entities are addressed by their GUID string (see scene files). */
typedef struct LumenHost
{
	uint32_t version;                                                   /* LUMEN_API_VERSION */
	void (*log)(int level, const char* message);
	double (*time)(void);                                               /* seconds since the game started */
	double (*delta_time)(void);
	int (*entity_find)(const char* name, char* id_out, size_t id_cap);  /* 1 when found */
	int (*entity_get_position)(const char* id, LumenVec3* out);
	int (*entity_set_position)(const char* id, LumenVec3 position);
	int (*entity_get_rotation)(const char* id, LumenVec3* euler_degrees);
	int (*entity_set_rotation)(const char* id, LumenVec3 euler_degrees);
	int (*key_down)(const char* key);                                   /* "w", "space", "mouse1" ... */
} LumenHost;

/* Required: called once after the game's Lua state exists, before scripts start. Return 0 on success. */
LUMEN_EXPORT int lumen_module_open(lua_State* L, const LumenHost* host);
/* Optional: called when the game stops (before the library is unloaded). */
LUMEN_EXPORT void lumen_module_close(lua_State* L);
/* Optional: called every frame / fixed step. */
LUMEN_EXPORT void lumen_module_update(double dt);
LUMEN_EXPORT void lumen_module_fixed_update(double dt);

#ifdef __cplusplus
}
#endif
#endif
