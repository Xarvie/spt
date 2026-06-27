/**
 * TestDeclare.c — 验证 `declare` 的 AST 契约 (未来 LSP 消费的表示)。
 *
 * declare 在运行期被擦除、不可观测,故其「符号 + 描述是否正确建模」必须在
 * 编译期通过 AST 直接断言。这里走 loadAst -> 检查 AstNode。
 *
 * 覆盖:
 *   - 环境声明 (declare <member>) 产生 NODE_FUNCTION_DECL/VARIABLE_DECL，is_ambient=true
 *   - 模块声明块 (declare from "...") 产生 NODE_DECLARE_MODULE，含 module_path/members/doc
 *   - 文档注释 (/// 与 /**) 挂到声明的 doc 字段，含多行累积与精确文本
 *   - 类声明成员 (字段/方法签名)
 *   - 多返回函数声明 (vars)
 *   - 非法形式被拒绝: auto / 初始化器 / 函数体
 */

#include "loadast.h"
#include "spt_ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed = 0;
#define CHECK(cond, msg)                                                                             \
  do {                                                                                              \
    if (!(cond)) {                                                                                  \
      printf("  FAIL: %s\n", msg);                                                                  \
      failed++;                                                                                     \
    }                                                                                               \
  } while (0)

static int streq(const char *a, const char *b) {
  if (!a || !b)
    return a == b;
  return strcmp(a, b) == 0;
}

/* 取根块的第 i 条语句。 */
static AstNode *stmt(AstNode *root, int i) {
  if (!root || root->type != NODE_BLOCK)
    return NULL;
  if (i < 0 || i >= root->u.block.statements.count)
    return NULL;
  return root->u.block.statements.items[i];
}

/* ---- 1. 环境函数声明 + 多行行文档 ---- */
static void test_ambient_func_with_doc(void) {
  printf("Testing: ambient func + doc...\n");
  AstNode *root = loadAst("/// hello doc\n/// second line\ndeclare int foo(int x);\n", "t1");
  CHECK(root != NULL, "loadAst should succeed");
  if (!root)
    return;
  CHECK(root->u.block.statements.count == 1, "one top-level statement");
  AstNode *s0 = stmt(root, 0);
  CHECK(s0 && s0->type == NODE_FUNCTION_DECL, "s0 is function decl");
  if (s0 && s0->type == NODE_FUNCTION_DECL) {
    CHECK(s0->u.func_decl.is_ambient, "func is ambient (erased)");
    CHECK(streq(s0->u.func_decl.name, "foo"), "func name 'foo'");
    CHECK(streq(s0->u.func_decl.doc, "hello doc\nsecond line"), "multi-line doc accumulated");
    CHECK(s0->u.func_decl.params.count == 1, "one param");
    CHECK(s0->u.func_decl.return_type &&
              s0->u.func_decl.return_type->type == NODE_TYPE_PRIMITIVE,
          "return type primitive (int)");
  }
  destroyAst(root);
}

/* ---- 2. 环境 const 变量声明 (无 doc / 无初始化器) ---- */
static void test_ambient_const_var(void) {
  printf("Testing: ambient const var...\n");
  AstNode *root = loadAst("declare const int FLAG;\n", "t2");
  CHECK(root != NULL, "loadAst should succeed");
  if (!root)
    return;
  AstNode *s0 = stmt(root, 0);
  CHECK(s0 && s0->type == NODE_VARIABLE_DECL, "s0 is variable decl");
  if (s0 && s0->type == NODE_VARIABLE_DECL) {
    CHECK(s0->u.var_decl.is_ambient, "var is ambient (erased)");
    CHECK(s0->u.var_decl.is_const, "var is const");
    CHECK(streq(s0->u.var_decl.name, "FLAG"), "var name 'FLAG'");
    CHECK(s0->u.var_decl.initializer == NULL, "no initializer");
    CHECK(s0->u.var_decl.doc == NULL, "no doc");
  }
  destroyAst(root);
}

/* ---- 3. 模块声明块 + 模块文档 + 成员文档 + 类成员 ---- */
static void test_declare_module(void) {
  printf("Testing: declare module block...\n");
  const char *src = "/// module doc\n"
                    "declare from \"sdl\" {\n"
                    "  /** init doc */\n"
                    "  int Init(int flags);\n"
                    "  const int INIT_VIDEO;\n"
                    "  class Window { void Destroy(); int w; }\n"
                    "}\n";
  AstNode *root = loadAst(src, "t3");
  CHECK(root != NULL, "loadAst should succeed");
  if (!root)
    return;
  AstNode *s0 = stmt(root, 0);
  CHECK(s0 && s0->type == NODE_DECLARE_MODULE, "s0 is declare-module");
  if (!s0 || s0->type != NODE_DECLARE_MODULE) {
    destroyAst(root);
    return;
  }
  CHECK(streq(s0->u.declare_module.module_path, "sdl"), "module path 'sdl'");
  CHECK(streq(s0->u.declare_module.doc, "module doc"), "module-level doc");
  CHECK(s0->u.declare_module.members.count == 3, "three members");

  AstNode *m0 = s0->u.declare_module.members.items[0];
  CHECK(m0 && m0->type == NODE_FUNCTION_DECL, "m0 func");
  if (m0 && m0->type == NODE_FUNCTION_DECL) {
    CHECK(m0->u.func_decl.is_ambient, "m0 ambient");
    CHECK(streq(m0->u.func_decl.name, "Init"), "m0 name 'Init'");
    CHECK(streq(m0->u.func_decl.doc, "init doc"), "m0 block doc");
    CHECK(m0->u.func_decl.params.count == 1, "m0 one param");
  }

  AstNode *m1 = s0->u.declare_module.members.items[1];
  CHECK(m1 && m1->type == NODE_VARIABLE_DECL, "m1 var");
  if (m1 && m1->type == NODE_VARIABLE_DECL) {
    CHECK(m1->u.var_decl.is_ambient, "m1 ambient");
    CHECK(streq(m1->u.var_decl.name, "INIT_VIDEO"), "m1 name 'INIT_VIDEO'");
    CHECK(m1->u.var_decl.is_const, "m1 const");
  }

  AstNode *m2 = s0->u.declare_module.members.items[2];
  CHECK(m2 && m2->type == NODE_CLASS_DECL, "m2 class");
  if (m2 && m2->type == NODE_CLASS_DECL) {
    CHECK(m2->u.class_decl.is_ambient, "m2 ambient");
    CHECK(streq(m2->u.class_decl.name, "Window"), "m2 name 'Window'");
    CHECK(m2->u.class_decl.members.count == 2, "Window has 2 members");
    if (m2->u.class_decl.members.count == 2) {
      AstNode *cm0 = m2->u.class_decl.members.items[0];
      AstNode *cm1 = m2->u.class_decl.members.items[1];
      CHECK(cm0 && cm0->type == NODE_CLASS_MEMBER, "cm0 class member");
      CHECK(cm1 && cm1->type == NODE_CLASS_MEMBER, "cm1 class member");
      AstNode *inner0 = cm0 ? cm0->u.class_member.member_declaration : NULL;
      AstNode *inner1 = cm1 ? cm1->u.class_member.member_declaration : NULL;
      CHECK(inner0 && inner0->type == NODE_FUNCTION_DECL &&
                streq(inner0->u.func_decl.name, "Destroy"),
            "Window.Destroy method");
      CHECK(inner1 && inner1->type == NODE_VARIABLE_DECL &&
                streq(inner1->u.var_decl.name, "w"),
            "Window.w field");
    }
  }
  destroyAst(root);
}

/* ---- 4. 多返回函数声明 (vars) ---- */
static void test_declare_multireturn(void) {
  printf("Testing: declare vars (multi-return)...\n");
  AstNode *root = loadAst("declare from \"m\" { vars GetVersion(); }\n", "t4");
  CHECK(root != NULL, "loadAst should succeed");
  if (!root)
    return;
  AstNode *s0 = stmt(root, 0);
  CHECK(s0 && s0->type == NODE_DECLARE_MODULE, "s0 declare-module");
  if (s0 && s0->type == NODE_DECLARE_MODULE && s0->u.declare_module.members.count == 1) {
    AstNode *m0 = s0->u.declare_module.members.items[0];
    CHECK(m0 && m0->type == NODE_FUNCTION_DECL, "m0 func");
    if (m0 && m0->type == NODE_FUNCTION_DECL) {
      CHECK(streq(m0->u.func_decl.name, "GetVersion"), "name 'GetVersion'");
      CHECK(m0->u.func_decl.return_type &&
                m0->u.func_decl.return_type->type == NODE_TYPE_MULTIRETURN,
            "return type multireturn");
    }
  }
  destroyAst(root);
}

/* ---- 5. 非法形式必须被拒绝 (loadAst 返回 NULL) ---- */
static void test_rejections(void) {
  printf("Testing: rejections (auto / initializer / body)...\n");
  AstNode *a = loadAst("declare auto x;\n", "r1");
  CHECK(a == NULL, "declare auto must be rejected");
  if (a)
    destroyAst(a);

  AstNode *b = loadAst("declare int x = 5;\n", "r2");
  CHECK(b == NULL, "declare with initializer must be rejected");
  if (b)
    destroyAst(b);

  AstNode *c = loadAst("declare int f(int a) { return a; }\n", "r3");
  CHECK(c == NULL, "declare with function body must be rejected");
  if (c)
    destroyAst(c);
}

int main(void) {
  printf("=== TestDeclare: declare AST contract ===\n");
  test_ambient_func_with_doc();
  test_ambient_const_var();
  test_declare_module();
  test_declare_multireturn();
  test_rejections();
  if (failed == 0) {
    printf("=== TestDeclare: ALL PASS ===\n");
    return 0;
  }
  printf("=== TestDeclare: %d CHECK(s) FAILED ===\n", failed);
  return 1;
}
