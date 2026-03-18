#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Ast/ast.h"
#include "ast_codegen.h"

extern "C" {
#include "Vm/lauxlib.h"
#include "Vm/lua.h"
#include "Vm/lualib.h"
#include "Vm/spt_module.h"
}

#define SPT_VERSION "0.1.0"
#define SPT_NAME "SPTScript"

static void printVersion() {
  std::cout << SPT_NAME << " " << SPT_VERSION << std::endl;
  std::cout << "Copyright (C) 2026 SPT Project" << std::endl;
}

static void printHelp(const char *progname) {
  std::cout << "Usage: " << progname << " [options] [script [args...]]" << std::endl;
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -e 'code'       Execute code string directly" << std::endl;
  std::cout << "  -v, --version   Show version information" << std::endl;
  std::cout << "  -h, --help      Show this help message" << std::endl;
  std::cout << "  -               Read script from stdin" << std::endl;
  std::cout << "  --              Stop processing options" << std::endl;
  std::cout << std::endl;
  std::cout << "Arguments after '--' or after script name are passed to the script." << std::endl;
  std::cout << std::endl;
  std::cout << "Examples:" << std::endl;
  std::cout << "  " << progname << " script.spt" << std::endl;
  std::cout << "  " << progname << " -e \"print('hello')\"" << std::endl;
  std::cout << "  " << progname << " script.spt arg1 arg2" << std::endl;
  std::cout << "  echo \"print('hello')\" | " << progname << " -" << std::endl;
}

static int runSource(const std::string &source, const std::string &chunkname,
                     const std::string &scriptDir, lua_State *L) {
  AstNode *ast = loadAst(source.c_str(), chunkname.c_str());
  if (!ast) {
    std::cerr << "Failed to parse AST" << std::endl;
    return -1;
  }

  Dyndata dyd = {0};
  std::string cn = std::string("@") + chunkname;

  LClosure *cl = astY_compile(L, ast, &dyd, cn.c_str());
  if (!cl) {
    std::cerr << "Failed to compile AST" << std::endl;
    destroyAst(ast);
    return -1;
  }

  int status = lua_pcall(L, 0, 0, 0);
  if (status != LUA_OK) {
    const char *err = lua_tostring(L, -1);
    std::cerr << "Runtime error: " << (err ? err : "unknown error") << std::endl;
    destroyAst(ast);
    return -1;
  }

  destroyAst(ast);
  return 0;
}

static int runScript(const std::string &path, const std::vector<std::string> &args) {
  std::string source;
  try {
    std::ifstream file(path);
    if (!file) {
      std::cerr << "Could not open file: " << path << std::endl;
      return -1;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    source = buffer.str();
  } catch (const std::exception &e) {
    std::cerr << "Error reading file: " << e.what() << std::endl;
    return -1;
  }

  std::filesystem::path scriptPath = std::filesystem::absolute(path);
  std::string filename = scriptPath.filename().string();
  std::string scriptDir = scriptPath.parent_path().string();

  lua_State *L = luaL_newstate();
  if (!L) {
    std::cerr << "Failed to create Lua state" << std::endl;
    return -1;
  }

  luaL_openlibs(L);
  spt_register_module_loader(L, scriptDir.c_str());

  if (!args.empty()) {
    lua_createtable(L, (int)args.size(), 0);
    for (size_t i = 0; i < args.size(); i++) {
      lua_pushstring(L, args[i].c_str());
      lua_rawseti(L, -2, (lua_Integer)i);
    }
    lua_setglobal(L, "arg");
  }

  int result = runSource(source, filename, scriptDir, L);
  lua_close(L);
  return result;
}

static int runStdin(const std::vector<std::string> &args) {
  std::stringstream buffer;
  buffer << std::cin.rdbuf();
  std::string source = buffer.str();

  lua_State *L = luaL_newstate();
  if (!L) {
    std::cerr << "Failed to create Lua state" << std::endl;
    return -1;
  }

  luaL_openlibs(L);
  spt_register_module_loader(L, ".");

  if (!args.empty()) {
    lua_createtable(L, (int)args.size(), 0);
    for (size_t i = 0; i < args.size(); i++) {
      lua_pushstring(L, args[i].c_str());
      lua_rawseti(L, -2, (lua_Integer)i);
    }
    lua_setglobal(L, "arg");
  }

  int result = runSource(source, "stdin", ".", L);
  lua_close(L);
  return result;
}

static int runCode(const std::string &code) {
  lua_State *L = luaL_newstate();
  if (!L) {
    std::cerr << "Failed to create Lua state" << std::endl;
    return -1;
  }

  luaL_openlibs(L);
  spt_register_module_loader(L, ".");

  int result = runSource(code, "command line", ".", L);
  lua_close(L);
  return result;
}

int main(int argc, char *argv[]) {
  bool showVersion = false;
  bool showHelp = false;
  bool stopOptions = false;
  std::string executeCode;
  std::string scriptFile;
  bool readStdin = false;
  std::vector<std::string> scriptArgs;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (stopOptions) {
      scriptArgs.push_back(arg);
      continue;
    }

    if (arg == "-v" || arg == "--version") {
      showVersion = true;
    } else if (arg == "-h" || arg == "--help") {
      showHelp = true;
    } else if (arg == "-e") {
      if (++i >= argc) {
        std::cerr << "Error: -e requires an argument" << std::endl;
        return -1;
      }
      executeCode = argv[i];
    } else if (arg == "-") {
      readStdin = true;
    } else if (arg == "--") {
      stopOptions = true;
    } else if (arg[0] == '-') {
      std::cerr << "Error: Unknown option: " << arg << std::endl;
      std::cerr << "Try '" << argv[0] << " --help' for more information." << std::endl;
      return -1;
    } else {
      if (scriptFile.empty()) {
        scriptFile = arg;
      } else {
        scriptArgs.push_back(arg);
      }
    }
  }

  if (showVersion) {
    printVersion();
    return 0;
  }

  if (showHelp) {
    printHelp(argv[0]);
    return 0;
  }

  if (!executeCode.empty()) {
    int result = runCode(executeCode);
    if (result != 0) {
      return result;
    }
  }

  if (readStdin) {
    return runStdin(scriptArgs);
  }

  if (!scriptFile.empty()) {
    return runScript(scriptFile, scriptArgs);
  }

  if (executeCode.empty()) {
    std::cerr << "Usage: " << argv[0] << " [options] [script [args...]]" << std::endl;
    std::cerr << "Try '" << argv[0] << " --help' for more information." << std::endl;
    return -1;
  }

  return 0;
}
