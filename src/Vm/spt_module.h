/*
** spt_module.h
** SPT Module System - Module loader for .spt files
*/

#ifndef spt_module_h
#define spt_module_h

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
** Register SPT module loader into package.searchers.
** This should be called after luaL_openlibs().
**
** The loader will search for .spt files in:
** 1. Current working directory
** 2. SPT_PATH environment variable (semicolon-separated paths)
** 3. Directory of the main script
*/
LUALIB_API void spt_register_module_loader(lua_State *L, const char *main_script_dir);

/*
** Set additional search paths for SPT modules.
** Paths are separated by semicolons.
*/
LUALIB_API void spt_set_module_path(lua_State *L, const char *path);

/*
** Get current SPT module search path.
** Returns a string that should NOT be freed.
*/
LUALIB_API const char *spt_get_module_path(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* spt_module_h */
