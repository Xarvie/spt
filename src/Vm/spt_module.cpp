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

/* -----------------------------------------------------------------------
** File / path helpers
** --------------------------------------------------------------------- */

static int readable(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (f == NULL)
    return 0;
  fclose(f);
  return 1;
}

/*
** Walk a ';'-separated path string, replacing each '?' with modname.
** Returns a malloc'd copy of the first readable filename found, or NULL.
** (Leaves nothing extra on the Lua stack.)
*/
static char *find_spt_file(lua_State *L, const char *modname) {
  const char *path = spt_search_path ? spt_search_path : "?.spt;./?.spt";

  /* Build the fully-substituted path string via luaL_addgsub */
  luaL_Buffer buff;
  luaL_buffinit(L, &buff);
  luaL_addgsub(&buff, path, "?", modname);
  luaL_addchar(&buff, '\0');
  luaL_pushresult(&buff); /* leaves string on stack */

  char *expanded = (char *)lua_tostring(L, -1);
  char *end = expanded + strlen(expanded);
  char *result = NULL;

  for (char *p = expanded; p < end;) {
    /* find next ';' separator */
    char *sep = (char *)memchr(p, ';', (size_t)(end - p));
    if (!sep)
      sep = end;
    char saved = *sep;
    *sep = '\0';

    if (readable(p)) {
      size_t len = (size_t)(sep - p) + 1;
      result = (char *)malloc(len);
      if (result)
        memcpy(result, p, len);
      *sep = saved;
      break;
    }

    *sep = saved;
    p = sep + 1;
  }

  lua_pop(L, 1); /* pop the expanded string */
  return result;
}

/* -----------------------------------------------------------------------
** Loader: called by ll_require with SPT slot-0 convention:
**   index 1 : nil  (receiver / slot 0)
**   index 2 : module name  (string)
**   index 3 : loader data  (filename, string)
** Must return 1 value: the module value.
** --------------------------------------------------------------------- */
static int spt_module_loader(lua_State *L) {
  /* Determine correct index for filename.
     SPT calls loaders as: loader(nil, modname, extra).
     Standard Lua calls as: loader(modname, extra).
     We accept both by checking arg count. */
  int nargs = lua_gettop(L);
  int filename_idx;
  if (nargs >= 3 && lua_type(L, 1) == LUA_TNIL) {
    /* SPT slot-0 convention: (nil, name, filename) */
    filename_idx = 3;
  } else if (nargs >= 2) {
    /* Standard Lua convention: (name, filename) */
    filename_idx = 2;
  } else {
    return luaL_error(L, "spt_module_loader: too few arguments");
  }

  const char *filename = luaL_checkstring(L, filename_idx);

  /* Read file */
  FILE *f = fopen(filename, "r");
  if (f == NULL)
    return luaL_error(L, "cannot open SPT file '%s'", filename);

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *source = (char *)malloc((size_t)fsize + 1);
  if (!source) {
    fclose(f);
    return luaL_error(L, "out of memory");
  }

  size_t nread = fread(source, 1, (size_t)fsize, f);
  source[nread] = '\0';
  fclose(f);

  /* Parse */
  AstNode *ast = loadAst(source, filename);
  free(source);
  if (!ast)
    return luaL_error(L, "failed to parse SPT file '%s'", filename);

  /* Compile */
  Dyndata dyd = {0};
  char chunkname[512];
  snprintf(chunkname, sizeof(chunkname), "@%s", filename);

  LClosure *cl = astY_compile(L, ast, &dyd, chunkname);
  destroyAst(ast);
  if (!cl)
    return luaL_error(L, "failed to compile SPT file '%s'", filename);

  /* cl is on top of the stack now. Call it with 0 args, 1 result.
     We drop any receiver/name/filename args below it first. */
  lua_insert(L, 1);  /* move closure to bottom of stack          */
  lua_settop(L, 1);  /* discard everything else, keep closure    */
  lua_call(L, 0, 1); /* call module body: () -> module_value     */

  return 1;
}

/* -----------------------------------------------------------------------
** Searcher: called by findloader with SPT slot-0 convention:
**   index 1 : nil  (receiver / slot 0)
**   index 2 : module name  (string)
** On success returns: loader_function, filename_string  (2 values)
** On failure returns: error_string                      (1 value)
** --------------------------------------------------------------------- */
static int spt_module_searcher(lua_State *L) {
  /* Accept both calling conventions (defensive) */
  const char *modname;
  if (lua_type(L, 1) == LUA_TNIL && lua_type(L, 2) == LUA_TSTRING) {
    modname = lua_tostring(L, 2); /* SPT: (nil, name) */
  } else if (lua_type(L, 1) == LUA_TSTRING) {
    modname = lua_tostring(L, 1); /* Standard: (name) */
  } else {
    lua_pushstring(L, "\n\tinvalid arguments to SPT searcher");
    return 1;
  }

  char *filename = find_spt_file(L, modname);
  if (!filename) {
    lua_pushfstring(L, "\n\tno file '%s.spt' in SPT search path", modname);
    return 1;
  }

  lua_pushcfunction(L, spt_module_loader);
  lua_pushstring(L, filename);
  free(filename);
  return 2; /* loader, filename */
}

/* -----------------------------------------------------------------------
** Registration helpers
** --------------------------------------------------------------------- */

/*
** Append fn to package.searchers using table.insert so that the
** TABLE_ARRAY logical-length is correctly updated (lua_rawseti alone
** does not extend loglen in this Lua variant, causing findloader to
** miss the new entry).
*/
static void append_searcher(lua_State *L, lua_CFunction fn) {
  /* Stack before: arbitrary */
  lua_getglobal(L, "package"); /* ... pkg               */
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return; /* package not available yet */
  }

  lua_getfield(L, -1, "searchers"); /* ... pkg searchers     */
  /* SPT Lua: searchers could be TABLE or ARRAY */
  int searchers_type = lua_type(L, -1);
  if (searchers_type != LUA_TTABLE && searchers_type != LUA_TARRAY) {
    lua_pop(L, 2);
    return;
  }

  /* Use table.insert(searchers, fn) to properly extend the sequence
   * SPT Lua calling convention: receiver is arg1, so we need 3 args total */
  lua_getglobal(L, "table");     /* ... pkg searchers tbl */
  lua_getfield(L, -1, "insert"); /* ... pkg searchers tbl insert */
  lua_remove(L, -2);             /* ... pkg searchers insert      */

  lua_pushnil(L);           /* ... pkg searchers insert nil (receiver) */
  lua_pushvalue(L, -3);     /* ... pkg searchers insert nil searchers */
  lua_pushcfunction(L, fn); /* ... pkg searchers insert nil searchers fn */
  lua_call(L, 3, 0);        /* table.insert(nil, searchers, fn)           */
                            /* ... pkg searchers                      */
  lua_pop(L, 2);            /* clean up pkg + searchers              */
}

/* -----------------------------------------------------------------------
** Public API
** --------------------------------------------------------------------- */

extern "C" {

/* Helper: convert Windows backslashes to forward slashes in-place */
static void normalize_path(char *path) {
  for (char *p = path; *p; p++) {
    if (*p == '\\')
      *p = '/';
  }
}

LUALIB_API void spt_register_module_loader(lua_State *L, const char *main_script_dir) {
  /* Build search path: script_dir/?.spt ; $SPT_PATH ; ./?.spt */
  luaL_Buffer pb;
  luaL_buffinit(L, &pb);

  if (main_script_dir && *main_script_dir) {
    /* Add path separator if needed */
    luaL_addstring(&pb, main_script_dir);
    luaL_addstring(&pb, "/?.spt;");
  }

  const char *env_path = getenv(SPT_PATH_VAR);
  if (env_path && *env_path) {
    luaL_addstring(&pb, env_path);
    luaL_addchar(&pb, ';');
  }

  luaL_addstring(&pb, "./?.spt");
  luaL_pushresult(&pb);

  if (spt_search_path)
    free(spt_search_path);
  spt_search_path = strdup(lua_tostring(L, -1));
  /* Normalize path separators to forward slashes */
  normalize_path(spt_search_path);
  lua_pop(L, 1);

  append_searcher(L, spt_module_searcher);
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

} /* extern "C" */