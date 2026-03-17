#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "Ast/ast.h"
#include "ast_codegen.h"

extern "C" {
#include "Vm/lauxlib.h"
#include "Vm/lua.h"
#include "Vm/lualib.h"
#include "Vm/spt_module.h"
}

int runScript(const char *path) {
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

  fprintf(stderr, "DEBUG runScript: absolute path='%s', scriptDir='%s'\n",
          scriptPath.string().c_str(), scriptDir.c_str());

  AstNode *ast = loadAst(source.c_str(), filename.c_str());
  if (!ast) {
    std::cerr << "Failed to parse AST" << std::endl;
    return -1;
  }

  lua_State *L = luaL_newstate();
  if (!L) {
    std::cerr << "Failed to create Lua state" << std::endl;
    destroyAst(ast);
    return -1;
  }

  luaL_openlibs(L);
  spt_register_module_loader(L, scriptDir.c_str());

  Dyndata dyd = {0};

  std::string chunkname = std::string("@") + filename;

  LClosure *cl = astY_compile(L, ast, &dyd, chunkname.c_str());
  if (!cl) {
    std::cerr << "Failed to compile AST" << std::endl;
    lua_close(L);
    destroyAst(ast);
    return -1;
  }

  int status = lua_pcall(L, 0, 0, 0);
  if (status != LUA_OK) {
    const char *err = lua_tostring(L, -1);
    std::cerr << "Runtime error: " << (err ? err : "unknown error") << std::endl;
    lua_close(L);
    destroyAst(ast);
    return -1;
  }

  lua_close(L);
  destroyAst(ast);
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc <= 1) {
    std::cerr << "Usage: " << argv[0] << " <script.spt>" << std::endl;
    return -1;
  }
  return runScript(argv[1]);
}
