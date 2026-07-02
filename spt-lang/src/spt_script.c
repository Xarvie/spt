/*
** spt_script.c — SPT 解释器命令行入口（纯 C，移植自 sptscript.cpp）。
**   流程：读源码 -> spt_frontend_parse -> astY_compile -> lua_pcall。
**   去除 iostream/filesystem，改用 stdio 与简易路径辅助。
*/
#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
/* Windows 的 <limits.h> 不定义 PATH_MAX，用 _MAX_PATH（stdlib.h）作下限 */
#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif
#endif

#include "spt_codegen.h"
#include "spt_frontend.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "spt_module.h"

#define SPT_VERSION "0.1.0"
#define SPT_NAME "SPTScript"

static void printVersion(void) {
  printf("%s %s\n", SPT_NAME, SPT_VERSION);
  printf("Copyright (C) 2026 SPT Project\n");
}

static void printHelp(const char *progname) {
  printf("Usage: %s [options] [script [args...]]\n\n", progname);
  printf("Options:\n");
  printf("  -e 'code'       Execute code string directly\n");
  printf("  -v, --version   Show version information\n");
  printf("  -h, --help      Show this help message\n");
  printf("  -               Read script from stdin\n");
  printf("  --              Stop processing options\n");
}

/* 读取整个文件到 malloc 缓冲（调用方负责 free）。 */
static char *read_file_all(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long n = ftell(f);
  if (n < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  char *buf = (char *)malloc((size_t)n + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t rd = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[rd] = '\0';
  return buf;
}

/* 读取 stdin 全部内容。 */
static char *read_stdin_all(void) {
  size_t cap = 4096, len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf)
    return NULL;
  size_t r;
  while ((r = fread(buf + len, 1, cap - len, stdin)) > 0) {
    len += r;
    if (len == cap) {
      cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if (!nb) {
        free(buf);
        return NULL;
      }
      buf = nb;
    }
  }
  buf[len] = '\0';
  return buf;
}

/* 取路径的目录部分（写入 out）；无分隔符则 "."。 */
static void path_dirname(const char *path, char *out, size_t outsz) {
  const char *slash = strrchr(path, '/');
#ifdef _WIN32
  const char *bslash = strrchr(path, '\\');
  if (bslash && (!slash || bslash > slash))
    slash = bslash;
#endif
  if (!slash) {
    snprintf(out, outsz, ".");
    return;
  }
  size_t len = (size_t)(slash - path);
  if (len == 0)
    len = 1; /* 根 "/" */
  if (len >= outsz)
    len = outsz - 1;
  memcpy(out, path, len);
  out[len] = '\0';
}

/* 取路径的文件名部分。 */
static const char *path_basename(const char *path) {
  const char *slash = strrchr(path, '/');
#ifdef _WIN32
  const char *bslash = strrchr(path, '\\');
  if (bslash && (!slash || bslash > slash))
    slash = bslash;
#endif
  return slash ? slash + 1 : path;
}

static int runSource(const char *source, const char *chunkname, lua_State *L) {
  AstNode *ast = spt_frontend_parse(source, chunkname);
  if (!ast) {
    fprintf(stderr, "Failed to parse AST\n");
    return -1;
  }

  Dyndata dyd = {0};
  char cn[600];
  snprintf(cn, sizeof(cn), "@%s", chunkname);

  LClosure *cl = astY_compile(L, ast, &dyd, cn);
  if (!cl) {
    fprintf(stderr, "Failed to compile AST\n");
    spt_frontend_destroy(ast);
    return -1;
  }

  int status = lua_pcall(L, 0, 0, 0);
  if (status != LUA_OK) {
    const char *err = lua_tostring(L, -1);
    fprintf(stderr, "Runtime error: %s\n", err ? err : "unknown error");
    spt_frontend_destroy(ast);
    return -1;
  }

  spt_frontend_destroy(ast);
  return 0;
}

/* 设置全局 arg 表（SPT 约定：args[i] 从 0 起）。 */
static void set_arg_table(lua_State *L, char **args, int nargs) {
  if (nargs <= 0)
    return;
  lua_createtable(L, nargs, 0);
  for (int i = 0; i < nargs; i++) {
    lua_pushstring(L, args[i]);
    lua_rawseti(L, -2, (lua_Integer)i);
  }
  lua_setglobal(L, "arg");
}

/* 解析绝对路径：跨平台封装。
** 成功返回指向 out 的指针，失败返回 NULL。out 缓冲应至少 PATH_MAX 字节。 */
static char *resolve_abspath(const char *path, char *out, size_t outsz) {
#ifdef _WIN32
  DWORD n = GetFullPathNameA(path, (DWORD)outsz, out, NULL);
  if (n == 0 || n >= outsz)
    return NULL;
  return out;
#else
  (void)outsz;
  return realpath(path, out);
#endif
}

static int runScript(const char *path, char **args, int nargs) {
  char *source = read_file_all(path);
  if (!source) {
    fprintf(stderr, "Could not open file: %s\n", path);
    return -1;
  }

  /* 解析绝对路径以确定脚本目录（失败则用原路径）。 */
  char abspath[PATH_MAX];
  const char *use_path = path;
  if (resolve_abspath(path, abspath, sizeof(abspath)) != NULL)
    use_path = abspath;

  char scriptDir[PATH_MAX];
  path_dirname(use_path, scriptDir, sizeof(scriptDir));

  lua_State *L = luaL_newstate();
  if (!L) {
    fprintf(stderr, "Failed to create Lua state\n");
    free(source);
    return -1;
  }

  luaL_openlibs(L);
  spt_register_module_loader(L, scriptDir);
  set_arg_table(L, args, nargs);

  /* [SPT] 用完整路径作 chunkname，使 debug.getinfo 的 source 字段包含目录
   * 信息（@完整路径），便于脚本通过 debug.getinfo 定位自身路径加载同目录
   * 文件。filename（basename）仍可用于 short_src 显示。 */
  int result = runSource(source, use_path, L);
  lua_close(L);
  free(source);
  return result;
}

static int runStdin(char **args, int nargs) {
  char *source = read_stdin_all();
  if (!source) {
    fprintf(stderr, "Failed to read stdin\n");
    return -1;
  }
  lua_State *L = luaL_newstate();
  if (!L) {
    fprintf(stderr, "Failed to create Lua state\n");
    free(source);
    return -1;
  }
  luaL_openlibs(L);
  spt_register_module_loader(L, ".");
  set_arg_table(L, args, nargs);
  int result = runSource(source, "stdin", L);
  lua_close(L);
  free(source);
  return result;
}

static int runCode(const char *code) {
  lua_State *L = luaL_newstate();
  if (!L) {
    fprintf(stderr, "Failed to create Lua state\n");
    return -1;
  }
  luaL_openlibs(L);
  spt_register_module_loader(L, ".");
  int result = runSource(code, "command line", L);
  lua_close(L);
  return result;
}

int main(int argc, char *argv[]) {
  int showVersion = 0, showHelp = 0, stopOptions = 0, readStdin = 0;
  const char *executeCode = NULL;
  const char *scriptFile = NULL;
  /* 脚本参数收集（最多 argc 个）。 */
  char **scriptArgs = (char **)malloc(sizeof(char *) * (size_t)(argc > 0 ? argc : 1));
  int nScriptArgs = 0;

  for (int i = 1; i < argc; i++) {
    char *arg = argv[i];
    if (stopOptions) {
      scriptArgs[nScriptArgs++] = arg;
      continue;
    }
    if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
      showVersion = 1;
    } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
      showHelp = 1;
    } else if (strcmp(arg, "-e") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "Error: -e requires an argument\n");
        free(scriptArgs);
        return -1;
      }
      executeCode = argv[i];
    } else if (strcmp(arg, "-") == 0) {
      readStdin = 1;
    } else if (strcmp(arg, "--") == 0) {
      stopOptions = 1;
    } else if (arg[0] == '-') {
      fprintf(stderr, "Error: Unknown option: %s\n", arg);
      fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
      free(scriptArgs);
      return -1;
    } else {
      if (!scriptFile)
        scriptFile = arg;
      else
        scriptArgs[nScriptArgs++] = arg;
    }
  }

  int rc = 0;
  if (showVersion) {
    printVersion();
  } else if (showHelp) {
    printHelp(argv[0]);
  } else {
    if (executeCode) {
      rc = runCode(executeCode);
      if (rc != 0) {
        free(scriptArgs);
        return rc;
      }
    }
    if (readStdin) {
      rc = runStdin(scriptArgs, nScriptArgs);
    } else if (scriptFile) {
      rc = runScript(scriptFile, scriptArgs, nScriptArgs);
    } else if (!executeCode) {
      fprintf(stderr, "Usage: %s [options] [script [args...]]\n", argv[0]);
      fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
      rc = -1;
    }
  }

  free(scriptArgs);
  return rc;
}
