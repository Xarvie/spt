/*
** spt_module.cpp
** SPT Module System - Module loader for .spt files
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "spt_module.h"

#include "../Ast/ast.h"
#include "../ast_codegen.h"

#define SPT_PATH_VAR "SPT_PATH"

static char *spt_search_path = NULL;

static int readable(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (f == NULL)
    return 0;
  fclose(f);
  return 1;
}

static const char *getnextfilename(char **path, char *end) {
  char *sep;
  char *name = *path;
  if (name == end)
    return NULL;
  sep = strchr(name, ';');
  if (sep == NULL)
    sep = end;
  *sep = '\0';
  *path = sep + 1;
  return name;
}

static char *find_spt_file(lua_State *L, const char *modname) {
  const char *path;
  luaL_Buffer buff;
  char *pathname;
  char *endpathname;
  const char *filename;
  char *result = NULL;

  path = spt_search_path;
  if (path == NULL)
    path = "?.spt;./?.spt";

  luaL_buffinit(L, &buff);
  luaL_addgsub(&buff, path, "?", modname);
  luaL_addchar(&buff, '\0');
  pathname = luaL_buffaddr(&buff);
  endpathname = pathname + luaL_bufflen(&buff) - 1;

  while ((filename = getnextfilename(&pathname, endpathname)) != NULL) {
    if (readable(filename)) {
      size_t len = strlen(filename) + 1;
      result = (char *)malloc(len);
      if (result)
        memcpy(result, filename, len);
      break;
    }
  }

  luaL_pushresult(&buff);
  lua_pop(L, 1);

  return result;
}

static int spt_module_loader(lua_State *L) {
  /* SPT: Stack layout after lua_call with Slot 0 Receiver:
     index 1: nil (receiver)
     index 2: module name
     index 3: loader data (filename)
  */
  const char *filename = luaL_checkstring(L, 3);

  FILE *f = fopen(filename, "r");
  if (f == NULL) {
    return luaL_error(L, "cannot open file '%s'", filename);
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *source = (char *)malloc(size + 1);
  if (source == NULL) {
    fclose(f);
    return luaL_error(L, "memory allocation error");
  }

  size_t read_size = fread(source, 1, size, f);
  source[read_size] = '\0';
  fclose(f);

  AstNode *ast = loadAst(source, filename);
  free(source);

  if (ast == NULL) {
    return luaL_error(L, "failed to parse '%s'", filename);
  }

  Dyndata dyd = {0};
  char chunkname[256];
  snprintf(chunkname, sizeof(chunkname), "@%s", filename);

  LClosure *cl = astY_compile(L, ast, &dyd, chunkname);
  destroyAst(ast);

  if (cl == NULL) {
    return luaL_error(L, "failed to compile '%s'", filename);
  }

  /* SPT: The closure is at stack top (index 4)
     We need to call it with nil as receiver (Slot 0 convention)
  */

  /* Remove name and filename, keep function */
  lua_settop(L, 4); /* stack: nil, name, filename, function */
  lua_remove(L, 3); /* stack: nil, name, function */
  lua_remove(L, 2); /* stack: nil, function */

  /* Now stack is: nil(receiver), function(closure) */
  /* Call with 0 arguments (receiver doesn't count) */
  lua_call(L, 0, 1);

  return 1;
}

static int spt_module_searcher(lua_State *L) {
  const char *modname = luaL_checkstring(L, 2);
  char *filename = find_spt_file(L, modname);

  if (filename == NULL) {
    lua_pushfstring(L, "\n\tno file '%s.spt' in SPT_PATH", modname);
    return 1;
  }

  lua_pushcfunction(L, spt_module_loader);
  lua_pushstring(L, filename);
  free(filename);

  return 2;
}

extern "C" {

LUALIB_API void spt_register_module_loader(lua_State *L, const char *main_script_dir) {
  luaL_Buffer path_buff;
  luaL_buffinit(L, &path_buff);

  if (main_script_dir && *main_script_dir) {
    luaL_addstring(&path_buff, main_script_dir);
    luaL_addstring(&path_buff, "/?.spt;");
  }

  const char *env_path = getenv(SPT_PATH_VAR);
  if (env_path) {
    luaL_addstring(&path_buff, env_path);
    luaL_addchar(&path_buff, ';');
  }

  luaL_addstring(&path_buff, "./?.spt");
  luaL_pushresult(&path_buff);

  const char *final_path = lua_tostring(L, -1);
  if (spt_search_path)
    free(spt_search_path);
  spt_search_path = strdup(final_path);
  lua_pop(L, 1);

  lua_getglobal(L, "require");

  if (lua_isfunction(L, -1)) {
    const char *uvname = lua_getupvalue(L, -1, 1);
    (void)uvname;

    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "searchers");

      if (lua_istable(L, -1)) {
        int len = 0;
        for (int i = 1; i <= 10; i++) {
          lua_rawgeti(L, -1, i);
          int type = lua_type(L, -1);
          if (type == LUA_TNIL) {
            lua_pop(L, 1);
            len = i - 1;
            break;
          }
          lua_pop(L, 1);
          if (i == 10)
            len = 10;
        }

        lua_pushcfunction(L, spt_module_searcher);
        lua_rawseti(L, -2, len + 1);
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}

LUALIB_API void spt_set_module_path(lua_State *L, const char *path) {
  (void)L;
  if (spt_search_path)
    free(spt_search_path);
  spt_search_path = path ? strdup(path) : NULL;
}

LUALIB_API const char *spt_get_module_path(lua_State *L) {
  (void)L;
  return spt_search_path ? spt_search_path : "?.spt;./?.spt";
}
}
