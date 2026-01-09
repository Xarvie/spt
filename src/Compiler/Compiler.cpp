#include "Compiler.h"
#include <algorithm>
#include <cassert>
#include <iostream>

namespace spt {

Compiler::Compiler(const std::string &moduleName, const std::string &source)
    : moduleName_(moduleName), source_(source), cg_(std::make_unique<CodeGen>(moduleName)) {}

Compiler::~Compiler() = default;

CompiledChunk Compiler::compile(AstNode *ast) {
  if (auto *block = dynamic_cast<BlockNode *>(ast)) {
    return compileModule(block);
  }
  error("Expected block node at top level");
  return {};
}

CompiledChunk Compiler::compileModule(BlockNode *block) {
  cg_->beginFunction(source_, moduleName_, 0, false, block);

  int envSlot = cg_->allocSlot();

  cg_->declareLocal("__env");
  cg_->current()->locals.push_back({"__env", envSlot, cg_->currentScopeDepth(), false});

  cg_->emitABC(OpCode::OP_NEWMAP, envSlot, 0, 0);

  for (auto *stmt : block->statements) {
    compileStatement(stmt);
  }

  cg_->emitABC(OpCode::OP_RETURN, 0, 2, 0);

  CompiledChunk chunk;
  chunk.moduleName = moduleName_;
  chunk.mainProto = cg_->endFunction();
  chunk.exports = exports_;
  return chunk;
}

void Compiler::compileStatement(Statement *stmt) {
  if (!stmt)
    return;

  cg_->setLineGetter(stmt);
  try {
    switch (stmt->nodeType) {
    case NodeType::BLOCK:
      compileBlock(static_cast<BlockNode *>(stmt));
      break;
    case NodeType::VARIABLE_DECL:
      compileVariableDecl(static_cast<VariableDeclNode *>(stmt));
      break;
    case NodeType::MUTI_VARIABLE_DECL:
      compileMutiVariableDecl(static_cast<MutiVariableDeclarationNode *>(stmt));
      break;
    case NodeType::FUNCTION_DECL:
      compileFunctionDecl(static_cast<FunctionDeclNode *>(stmt));
      break;
    case NodeType::CLASS_DECL:
      compileClassDecl(static_cast<ClassDeclNode *>(stmt));
      break;
    case NodeType::IF_STATEMENT:
      compileIfStatement(static_cast<IfStatementNode *>(stmt));
      break;
    case NodeType::WHILE_STATEMENT:
      compileWhileStatement(static_cast<WhileStatementNode *>(stmt));
      break;
    case NodeType::FOR_CSTYLE_STATEMENT:
      compileForCStyle(static_cast<ForCStyleStatementNode *>(stmt));
      break;
    case NodeType::FOR_EACH_STATEMENT:
      compileForEach(static_cast<ForEachStatementNode *>(stmt));
      break;
    case NodeType::RETURN_STATEMENT:
      compileReturn(static_cast<ReturnStatementNode *>(stmt));
      break;
    case NodeType::BREAK_STATEMENT:
      compileBreak(static_cast<BreakStatementNode *>(stmt));
      break;
    case NodeType::CONTINUE_STATEMENT:
      compileContinue(static_cast<ContinueStatementNode *>(stmt));
      break;
    case NodeType::ASSIGNMENT:
      compileAssignment(static_cast<AssignmentNode *>(stmt));
      break;
    case NodeType::UPDATE_ASSIGNMENT:
      compileUpdateAssignment(static_cast<UpdateAssignmentNode *>(stmt));
      break;
    case NodeType::EXPRESSION_STATEMENT:
      compileExpressionStatement(static_cast<ExpressionStatementNode *>(stmt));
      break;
    case NodeType::IMPORT_NAMESPACE: {
      compileImportNamespace(static_cast<ImportNamespaceNode *>(stmt));
      break;
    }
    case NodeType::IMPORT_NAMED: {
      compileImportNamed(static_cast<ImportNamedNode *>(stmt));
      break;
    }
    case NodeType::DEFER_STATEMENT:
      compileDefer(static_cast<DeferStatementNode *>(stmt));
      break;

    default:
      error("Unknown statement type", stmt->location);
    }
  } catch (const std::runtime_error &e) {
    error(e.what(), stmt->location);
  }
}

void Compiler::compileDefer(DeferStatementNode *node) {

  cg_->beginFunction(source_, "<defer>", 0, false, node);

  if (node->body) {

    compileBlock(node->body);
  }

  cg_->emitABC(OpCode::OP_RETURN, 0, 1, 0);

  Prototype childProto = cg_->endFunction();
  int protoIdx = static_cast<int>(cg_->current()->proto.protos.size());
  cg_->current()->proto.protos.push_back(std::move(childProto));

  int closureSlot = cg_->allocSlot();
  cg_->emitABx(OpCode::OP_CLOSURE, closureSlot, protoIdx);

  cg_->emitABx(OpCode::OP_DEFER, closureSlot, 0);

  cg_->freeSlots(1);
}

void Compiler::compileBlock(BlockNode *block) {
  block->useEnd = false;
  cg_->setLineGetter(block);
  cg_->beginScope();
  for (auto *stmt : block->statements) {
    compileStatement(stmt);
  }
  cg_->endScope();
  cg_->setLineGetter(block);
  block->useEnd = true;
}

void Compiler::compileVariableDecl(VariableDeclNode *decl) {
  cg_->setLineGetter(decl);
  int slot = cg_->addLocal(decl->name);

  if (decl->initializer) {
    compileExpression(decl->initializer, slot);
  } else {
    cg_->emitABC(OpCode::OP_LOADNIL, slot, 0, 0);
  }

  cg_->markInitialized();

  if (decl->isExported) {
    exports_.push_back(decl->name);
  }

  if (decl->isGlobal || decl->isModuleRoot) {
    emitStoreToEnv(decl->name, slot);
  }
}

void Compiler::compileMutiVariableDecl(MutiVariableDeclarationNode *decl) {
  if (decl->initializer) {
    int numVars = static_cast<int>(decl->variables.size());
    int baseSlot = cg_->allocSlots(numVars);

    if (auto *callNode = dynamic_cast<FunctionCallNode *>(decl->initializer)) {
      cg_->setLineGetter(callNode);
      compileFunctionCall(callNode, baseSlot, numVars);
    } else {
      compileExpression(decl->initializer, baseSlot);
      if (numVars > 1) {
        cg_->emitABC(OpCode::OP_LOADNIL, baseSlot + 1, numVars - 1, 0);
      }
    }

    for (size_t i = 0; i < decl->variables.size(); ++i) {
      const auto &var = decl->variables[i];

      cg_->addLocal(var.name);
      cg_->current()->locals.back().slot = baseSlot + static_cast<int>(i);
      if (decl->isExported)
        exports_.push_back(var.name);

      if (decl->isModuleRoot) {
        int slot = baseSlot + static_cast<int>(i);
        int nameIdx = cg_->addStringConstant(var.name);
        if (nameIdx <= 255) {
          cg_->emitABC(OpCode::OP_SETFIELD, 0, nameIdx, slot);
        } else {
          int keySlot = cg_->allocSlot();
          cg_->emitABx(OpCode::OP_LOADK, keySlot, nameIdx);
          cg_->emitABC(OpCode::OP_SETINDEX, 0, keySlot, slot);
          cg_->freeSlots(1);
        }
      }
    }
  } else {

    for (const auto &var : decl->variables) {
      int slot = cg_->addLocal(var.name);
      cg_->emitABC(OpCode::OP_LOADNIL, slot, 0, 0);
      if (decl->isExported)
        exports_.push_back(var.name);
      if (decl->isModuleRoot) {
        int nameIdx = cg_->addStringConstant(var.name);
        if (nameIdx <= 255) {
          cg_->emitABC(OpCode::OP_SETFIELD, 0, nameIdx, slot);
        } else {
          int keySlot = cg_->allocSlot();
          cg_->emitABx(OpCode::OP_LOADK, keySlot, nameIdx);
          cg_->emitABC(OpCode::OP_SETINDEX, 0, keySlot, slot);
          cg_->freeSlots(1);
        }
      }
    }
  }
}

void Compiler::compileFunctionDecl(FunctionDeclNode *node) {
  int nameSlot = cg_->addLocal(node->name);
  cg_->markInitialized();

  int numParams = static_cast<int>(node->params.size());
  cg_->beginFunction(source_, node->name, numParams, node->isVariadic, node);

  if (!node->params.empty() && node->params[0]->name == "this") {
    cg_->current()->proto.needsReceiver = true;
  } else {
    cg_->current()->proto.needsReceiver = false;
  }
  // =========================================================

  int paramIndex = 0;
  for (auto *param : node->params) {
    cg_->setLineGetter(param);
    cg_->addLocal(param->name);
    cg_->current()->locals.back().slot = paramIndex++;
    cg_->markInitialized();
  }

  compileBlock(node->body);

  cg_->emitABC(OpCode::OP_RETURN, 0, 1, 0);

  Prototype childProto = cg_->endFunction();
  int protoIdx = static_cast<int>(cg_->current()->proto.protos.size());
  cg_->current()->proto.protos.push_back(std::move(childProto));

  cg_->emitABx(OpCode::OP_CLOSURE, nameSlot, protoIdx);

  if (node->isExported) {
    exports_.push_back(node->name);
  }

  if (node->isGlobalDecl || node->isModuleRoot) {
    emitStoreToEnv(node->name, nameSlot);
  }
}

void Compiler::compileFunctionCall(FunctionCallNode *node, int dest, int nResults) {
  cg_->setLineGetter(node);

  if (auto *memberAccess = dynamic_cast<MemberAccessNode *>(node->functionExpr)) {
    compileMethodInvoke(memberAccess->objectExpr, memberAccess->memberName, node->arguments, dest,
                        nResults);
    return;
  }
  if (auto *memberLookup = dynamic_cast<MemberLookupNode *>(node->functionExpr)) {
    compileMethodInvoke(memberLookup->objectExpr, memberLookup->memberName, node->arguments, dest,
                        nResults);
    return;
  }

  int funcSlot = cg_->allocSlot();
  compileExpression(node->functionExpr, funcSlot);

  for (auto *arg : node->arguments) {
    int argSlot = cg_->allocSlot();
    compileExpression(arg, argSlot);
  }

  int argCount = static_cast<int>(node->arguments.size());

  cg_->emitABC(OpCode::OP_CALL, funcSlot, argCount + 1, nResults + 1);

  if (nResults > 0 && dest != funcSlot) {
    for (int i = 0; i < nResults; ++i) {
      cg_->emitABC(OpCode::OP_MOVE, dest + i, funcSlot + i, 0);
    }
  }

  cg_->freeSlots(argCount + 1);
}

void Compiler::compileMethodInvoke(Expression *receiverExpr, const std::string &methodName,
                                   const std::vector<Expression *> &arguments, int dest,
                                   int nResults) {

  cg_->setLineGetter(receiverExpr);

  int argCount = static_cast<int>(arguments.size());
  int totalArgs = 1 + argCount;

  int methodIdx = cg_->addStringConstant(methodName);

  if (methodIdx > 255) {
    compileMethodInvokeFallback(receiverExpr, methodName, methodIdx, arguments, dest, nResults);
    return;
  }

  int base = cg_->allocSlot();
  compileExpression(receiverExpr, base);

  for (int i = 0; i < argCount; ++i) {
    int argSlot = cg_->allocSlot();
    compileExpression(arguments[i], argSlot);


  }

  cg_->emitABC(OpCode::OP_INVOKE, base, totalArgs, methodIdx);

  if (nResults > 0 && dest != base) {
    for (int i = 0; i < nResults; ++i) {
      cg_->emitABC(OpCode::OP_MOVE, dest + i, base + i, 0);
    }
  }

  cg_->freeSlots(totalArgs);
}

void Compiler::compileMethodInvokeFallback(Expression *receiverExpr, const std::string &methodName,
                                           int methodIdx,
                                           const std::vector<Expression *> &arguments, int dest,
                                           int nResults) {
  int argCount = static_cast<int>(arguments.size());
  int methodSlot = cg_->allocSlot();

  int receiverSlot = cg_->allocSlot();
  compileExpression(receiverExpr, receiverSlot);

  int keySlot = cg_->allocSlot();
  cg_->emitABx(OpCode::OP_LOADK, keySlot, methodIdx);
  cg_->emitABC(OpCode::OP_GETINDEX, methodSlot, receiverSlot, keySlot);
  cg_->freeSlots(1);

  for (int i = 0; i < argCount; ++i) {
    int argSlot = cg_->allocSlot();
    compileExpression(arguments[i], argSlot);
  }

  cg_->emitABC(OpCode::OP_CALL, methodSlot, argCount + 2, nResults + 1);

  if (nResults > 0 && dest != methodSlot) {
    for (int i = 0; i < nResults; ++i) {
      cg_->emitABC(OpCode::OP_MOVE, dest + i, methodSlot + i, 0);
    }
  }

  cg_->freeSlots(2 + argCount);
}


void Compiler::compileClassDecl(ClassDeclNode *decl) {
  int slot = cg_->addLocal(decl->name);

  cg_->markInitialized();

  int nameIdx = cg_->addStringConstant(decl->name);
  cg_->emitABx(OpCode::OP_NEWCLASS, slot, nameIdx);

  cg_->beginClass(decl->name);

  for (auto *member : decl->members) {
    auto *memberDecl = member->memberDeclaration;

    if (auto *func = dynamic_cast<FunctionDeclNode *>(memberDecl)) {
      int methodNameIdx = cg_->addStringConstant(func->name);
      int tempSlot = cg_->allocSlot();

      int numParams = static_cast<int>(func->params.size());

      cg_->beginFunction(source_, func->name, numParams, func->isVariadic, func);

      if (!func->params.empty() && func->params[0]->name == "this") {
        cg_->current()->proto.needsReceiver = true;
      } else {
        cg_->current()->proto.needsReceiver = false;
      }

      int paramIndex = 0;
      for (auto *param : func->params) {
        cg_->addLocal(param->name);
        cg_->current()->locals.back().slot = paramIndex++;
        cg_->markInitialized();
      }

      if (func->body) {
        compileBlock(func->body);
      }

      cg_->emitABC(OpCode::OP_RETURN, 0, 1, 0);

      Prototype childProto = cg_->endFunction();
      int protoIdx = static_cast<int>(cg_->current()->proto.protos.size());
      cg_->current()->proto.protos.push_back(std::move(childProto));

      cg_->emitABx(OpCode::OP_CLOSURE, tempSlot, protoIdx);

      if (methodNameIdx <= 255) {
        cg_->emitABC(OpCode::OP_SETFIELD, slot, methodNameIdx, tempSlot);
      } else {
        int keySlot = cg_->allocSlot();
        cg_->emitABx(OpCode::OP_LOADK, keySlot, methodNameIdx);
        cg_->emitABC(OpCode::OP_SETINDEX, slot, keySlot, tempSlot);
        cg_->freeSlots(1);
      }
      cg_->freeSlots(1);
    }
  }

  cg_->endClass();

  if (decl->isExported) {
    exports_.push_back(decl->name);
  }

  if (decl->isModuleRoot) {
    emitStoreToEnv(decl->name, slot);
  }
}

static bool isSmallInt(Expression *expr, int8_t &outVal) {
  if (auto *intNode = dynamic_cast<LiteralIntNode *>(expr)) {
    int64_t v = intNode->value;
    if (v >= -128 && v <= 127) {
      outVal = static_cast<int8_t>(v);
      return true;
    }
  }
  return false;
}

static int tryAddConstantForEQK(CodeGen *cg, Expression *expr) {
  ConstantValue val;
  bool isValid = false;

  if (dynamic_cast<LiteralNullNode *>(expr)) {
    val = nullptr;
    isValid = true;
  } else if (auto *b = dynamic_cast<LiteralBoolNode *>(expr)) {
    val = b->value;
    isValid = true;
  } else if (auto *i = dynamic_cast<LiteralIntNode *>(expr)) {
    val = i->value;
    isValid = true;
  } else if (auto *f = dynamic_cast<LiteralFloatNode *>(expr)) {
    val = f->value;
    isValid = true;
  }

  if (isValid) {
    int idx = cg->addConstant(val);

    if (idx <= 255)
      return idx;
  }
  return -1;
}

int Compiler::compileCondition(Expression *expr) {

  if (auto *bin = dynamic_cast<BinaryOpNode *>(expr)) {
    if (isComparisonOp(bin->op)) {
      int leftSlot = cg_->allocSlot();
      compileExpression(bin->left, leftSlot);

      int8_t imm = 0;
      int constIdx = -1;
      OpCode op = OpCode::OP_EQ;
      bool optimized = false;

      int k = 0;

      bool isEqNe = (bin->op == OperatorKind::EQ || bin->op == OperatorKind::NE);
      bool isLtGe = (bin->op == OperatorKind::LT || bin->op == OperatorKind::GE);
      bool isLeGt = (bin->op == OperatorKind::LE || bin->op == OperatorKind::GT);

      if (bin->op == OperatorKind::NE || bin->op == OperatorKind::GE ||
          bin->op == OperatorKind::GT) {
        k = 1;
      }

      if (isEqNe) {
        if (isSmallInt(bin->right, imm)) {
          op = OpCode::OP_EQI;
          optimized = true;
        } else if ((constIdx = tryAddConstantForEQK(cg_.get(), bin->right)) != -1) {
          op = OpCode::OP_EQK;
          optimized = true;
        }
      } else if (isLtGe) {
        if (isSmallInt(bin->right, imm)) {
          op = OpCode::OP_LTI;
          optimized = true;
        }
      } else if (isLeGt) {
        if (isSmallInt(bin->right, imm)) {
          op = OpCode::OP_LEI;
          optimized = true;
        }
      }

      if (optimized) {

        uint8_t bVal =
            (op == OpCode::OP_EQK) ? static_cast<uint8_t>(constIdx) : static_cast<uint8_t>(imm);
        cg_->emitABC(op, leftSlot, bVal, k);
      } else {

        int rightSlot = cg_->allocSlot();
        compileExpression(bin->right, rightSlot);

        OpCode stdOp = OpCode::OP_EQ;
        if (isLtGe)
          stdOp = OpCode::OP_LT;
        else if (isLeGt)
          stdOp = OpCode::OP_LE;

        cg_->emitABC(stdOp, leftSlot, rightSlot, k);
        cg_->freeSlots(1);
      }
      cg_->freeSlots(1);

      return cg_->emitJump(OpCode::OP_JMP);
    }
  }

  int slot = cg_->allocSlot();
  compileExpression(expr, slot);

  cg_->emitABC(OpCode::OP_TEST, slot, 0, 0);

  cg_->freeSlots(1);

  return cg_->emitJump(OpCode::OP_JMP);
}

void Compiler::compileIfStatement(IfStatementNode *stmt) {

  int jumpToElse = compileCondition(stmt->condition);

  compileBlock(stmt->thenBlock);

  std::vector<int> endJumps;

  if (!stmt->elseIfClauses.empty() || stmt->elseBlock) {

    int jumpToEnd = cg_->emitJump(OpCode::OP_JMP);
    endJumps.push_back(jumpToEnd);
  }

  cg_->patchJump(jumpToElse);

  for (auto *elseIf : stmt->elseIfClauses) {
    int jumpToNext = compileCondition(elseIf->condition);

    compileBlock(elseIf->body);

    endJumps.push_back(cg_->emitJump(OpCode::OP_JMP));

    cg_->patchJump(jumpToNext);
  }

  if (stmt->elseBlock) {
    compileBlock(stmt->elseBlock);
  }

  for (int jump : endJumps) {
    cg_->patchJump(jump);
  }
}

void Compiler::compileWhileStatement(WhileStatementNode *stmt) {
  int loopStart = cg_->currentPc();
  cg_->beginLoop(loopStart);

  int condSlot = cg_->allocSlot();
  compileExpression(stmt->condition, condSlot);

  cg_->emitABC(OpCode::OP_TEST, condSlot, 0, 0);
  int exitJump = cg_->emitJump(OpCode::OP_JMP);

  cg_->freeSlots(1);

  compileBlock(stmt->body);

  int loopJump = cg_->currentPc() - loopStart;
  cg_->emitAsBx(OpCode::OP_JMP, 0, -loopJump - 1);

  cg_->patchJump(exitJump);
  cg_->patchBreaks();
  cg_->endLoop();
}

static Expression *getStepExpression(const std::string &varName, Statement *updateStmt) {
  if (auto *ua = dynamic_cast<UpdateAssignmentNode *>(updateStmt)) {
    if (auto *id = dynamic_cast<IdentifierNode *>(ua->lvalue)) {
      if (id->name == varName && ua->op == OperatorKind::ASSIGN_ADD) {
        return ua->rvalue;
      }
    }
  }

  else if (auto *assign = dynamic_cast<AssignmentNode *>(updateStmt)) {
    if (assign->lvalues.size() == 1 && assign->rvalues.size() == 1) {
      auto *lId = dynamic_cast<IdentifierNode *>(assign->lvalues[0]);
      if (lId && lId->name == varName) {
        auto *binOp = dynamic_cast<BinaryOpNode *>(assign->rvalues[0]);
        if (binOp && binOp->op == OperatorKind::ADD) {
          auto *leftId = dynamic_cast<IdentifierNode *>(binOp->left);
          if (leftId && leftId->name == varName)
            return binOp->right;

          auto *rightId = dynamic_cast<IdentifierNode *>(binOp->right);
          if (rightId && rightId->name == varName)
            return binOp->left;
        }
      }
    }
  }
  return nullptr;
}

bool Compiler::tryCompileNumericLoop(ForCStyleStatementNode *stmt) {
  if (!stmt->initializer ||
      !std::holds_alternative<std::vector<Declaration *>>(*stmt->initializer)) {
    return false;
  }

  auto &decls = std::get<std::vector<Declaration *>>(*stmt->initializer);
  if (decls.size() != 1)
    return false;

  auto *varDecl = dynamic_cast<VariableDeclNode *>(decls[0]);
  if (!varDecl)
    return false;
  std::string varName = varDecl->name;

  auto *binOp = dynamic_cast<BinaryOpNode *>(stmt->condition);
  if (!binOp)
    return false;

  auto *leftId = dynamic_cast<IdentifierNode *>(binOp->left);
  if (!leftId || leftId->name != varName)
    return false;

  if (binOp->op != OperatorKind::LT && binOp->op != OperatorKind::LE)
    return false;

  if (stmt->updateActions.size() != 1)
    return false;
  Expression *stepExpr = getStepExpression(varName, stmt->updateActions[0]);
  if (!stepExpr)
    return false;

  cg_->beginScope();

  cg_->setLineGetter(varDecl);
  int indexSlot = cg_->addLocal(varName);
  if (varDecl->initializer) {
    compileExpression(varDecl->initializer, indexSlot);
  } else {
    cg_->emitABC(OpCode::OP_LOADNIL, indexSlot, 0, 0);
  }
  cg_->markInitialized();

  int limitSlot = cg_->allocSlot();
  compileExpression(binOp->right, limitSlot);

  if (binOp->op == OperatorKind::LT) {
    cg_->emitABC(OpCode::OP_ADDI, limitSlot, limitSlot, static_cast<uint8_t>(-1));
  }

  int stepSlot = cg_->allocSlot();
  compileExpression(stepExpr, stepSlot);

  int forPrepPc = cg_->currentPc();
  cg_->emitAsBx(OpCode::OP_FORPREP, indexSlot, 0);
  int prepJump = forPrepPc;

  cg_->beginLoop(forPrepPc + 1);

  compileBlock(stmt->body);

  int loopEndPc = cg_->currentPc();

  cg_->patchContinues(loopEndPc);

  int jumpBackOffset = forPrepPc - loopEndPc;
  cg_->emitAsBx(OpCode::OP_FORLOOP, indexSlot, jumpBackOffset);

  cg_->patchBreaks();
  cg_->endLoop();

  cg_->patchJumpTo(prepJump, loopEndPc);

  cg_->endScope();
  return true;
}

void Compiler::compileForCStyle(ForCStyleStatementNode *stmt) {
  if (tryCompileNumericLoop(stmt)) {
    return;
  }
  cg_->beginScope();

  if (stmt->initializer) {
    std::visit(
        [this](auto &&arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, std::vector<Declaration *>>) {
            for (auto *decl : arg) {
              if (auto *varDecl = dynamic_cast<VariableDeclNode *>(decl)) {
                compileVariableDecl(varDecl);
              }
            }
          } else if constexpr (std::is_same_v<T, AssignmentNode *>) {
            compileAssignment(arg);
          } else if constexpr (std::is_same_v<T, std::vector<Expression *>>) {
            for (auto *expr : arg) {
              compileExpressionForValue(expr);
              cg_->freeSlots(1);
            }
          }
        },
        *stmt->initializer);
  }

  int loopStart = cg_->currentPc();
  cg_->beginLoop(loopStart);
  int exitJump = -1;

  if (stmt->condition) {
    int condSlot = cg_->allocSlot();
    compileExpression(stmt->condition, condSlot);

    cg_->emitABC(OpCode::OP_TEST, condSlot, 0, 0);
    exitJump = cg_->emitJump(OpCode::OP_JMP);
    cg_->freeSlots(1);
  }

  compileBlock(stmt->body);

  int continueTarget = cg_->currentPc();
  for (auto *update : stmt->updateActions) {
    compileStatement(update);
  }

  int loopJump = cg_->currentPc() - loopStart;
  cg_->emitAsBx(OpCode::OP_JMP, 0, -loopJump - 1);

  if (exitJump >= 0)
    cg_->patchJump(exitJump);
  cg_->patchBreaks();
  cg_->patchContinues(continueTarget);
  cg_->endLoop();
  cg_->endScope();
}

void Compiler::compileForEach(ForEachStatementNode *stmt) {
  cg_->beginScope();

  int iterSlot = cg_->allocSlot();
  compileExpression(stmt->iterableExpr, iterSlot);

  std::vector<int> varSlots;
  for (auto *param : stmt->loopVariables) {
    int slot = cg_->addLocal(param->name);
    varSlots.push_back(slot);
  }

  int loopStart = cg_->currentPc();
  cg_->beginLoop(loopStart);

  int resultSlot = varSlots.empty() ? cg_->allocSlot() : varSlots[0];

  cg_->emitABC(OpCode::OP_CALL, iterSlot, 1, static_cast<uint8_t>(varSlots.size() + 1));

  cg_->emitABC(OpCode::OP_TEST, resultSlot, 0, 0);
  int exitJump = cg_->emitJump(OpCode::OP_JMP);

  compileBlock(stmt->body);

  int loopJump = cg_->currentPc() - loopStart;
  cg_->emitAsBx(OpCode::OP_JMP, 0, -loopJump - 1);

  cg_->patchJump(exitJump);
  cg_->patchBreaks();
  cg_->endLoop();
  cg_->endScope();
}

void Compiler::compileReturn(ReturnStatementNode *stmt) {
  if (stmt->returnValue.empty()) {
    cg_->emitABC(OpCode::OP_RETURN, 0, 1, 0);
  } else if (stmt->returnValue.size() == 1) {
    int slot = cg_->allocSlot();
    compileExpression(stmt->returnValue[0], slot);
    cg_->emitABC(OpCode::OP_RETURN, slot, 2, 0);
    cg_->freeSlots(1);
  } else {
    int base = cg_->allocSlots(static_cast<int>(stmt->returnValue.size()));
    for (size_t i = 0; i < stmt->returnValue.size(); ++i) {
      compileExpression(stmt->returnValue[i], base + static_cast<int>(i));
    }
    cg_->emitABC(OpCode::OP_RETURN, base, static_cast<uint8_t>(stmt->returnValue.size() + 1), 0);
    cg_->freeSlots(static_cast<int>(stmt->returnValue.size()));
  }
}

void Compiler::compileBreak(BreakStatementNode *) {

  int jump = cg_->emitJump(OpCode::OP_JMP);

  if (cg_->current()->loops.empty()) {
    error("'break' outside of loop");
    return;
  }
  cg_->current()->loops.back().breakJumps.push_back({jump, 0});
}

void Compiler::compileContinue(ContinueStatementNode *) {
  if (cg_->current()->loops.empty()) {
    error("'continue' outside of loop");
    return;
  }
  int jump = cg_->emitJump(OpCode::OP_JMP);
  cg_->current()->loops.back().continueJumps.push_back({jump, 0});
}

void Compiler::compileAssignment(AssignmentNode *stmt) {
  std::vector<int> valueSlots;

  for (auto *rval : stmt->rvalues) {
    int slot = cg_->allocSlot();
    compileExpression(rval, slot);
    valueSlots.push_back(slot);
  }

  for (size_t i = 0; i < stmt->lvalues.size(); ++i) {
    int srcSlot = (i < valueSlots.size()) ? valueSlots[i] : valueSlots.back();

    LValue lv = compileLValue(stmt->lvalues[i]);

    emitStore(lv, srcSlot);
  }

  cg_->freeSlots(static_cast<int>(valueSlots.size()));
}

void Compiler::compileUpdateAssignment(UpdateAssignmentNode *stmt) {
  LValue lv = compileLValue(stmt->lvalue);

  int leftSlot = cg_->allocSlot();
  int rightSlot = cg_->allocSlot();

  switch (lv.kind) {
  case LValue::LOCAL:
    cg_->emitABC(OpCode::OP_MOVE, leftSlot, lv.a, 0);
    break;
  case LValue::UPVALUE:
    cg_->emitABC(OpCode::OP_GETUPVAL, leftSlot, lv.a, 0);
    break;
  case LValue::GLOBAL:
    cg_->emitABx(OpCode::OP_GETFIELD, leftSlot, lv.a);
    break;
  case LValue::FIELD:
    cg_->emitABC(OpCode::OP_GETFIELD, leftSlot, lv.a, lv.b);
    break;
  case LValue::INDEX:
    cg_->emitABC(OpCode::OP_GETINDEX, leftSlot, lv.a, lv.b);
    break;
  }

  compileExpression(stmt->rvalue, rightSlot);

  OpCode op = binaryOpToOpcode(stmt->op);
  cg_->emitABC(op, leftSlot, leftSlot, rightSlot);

  emitStore(lv, leftSlot);
  cg_->freeSlots(2);
}

void Compiler::compileExpressionStatement(ExpressionStatementNode *stmt) {
  int slot = cg_->allocSlot();
  compileExpression(stmt->expression, slot);
  cg_->freeSlots(1);
}

void Compiler::compileExpression(Expression *expr, int dest) {
  cg_->setLineGetter(expr);
  if (!expr) {
    cg_->emitABC(OpCode::OP_LOADNIL, dest, 0, 0);
    return;
  }

  switch (expr->nodeType) {
  case NodeType::LITERAL_INT:
  case NodeType::LITERAL_FLOAT:
  case NodeType::LITERAL_STRING:
  case NodeType::LITERAL_BOOL:
  case NodeType::LITERAL_NULL:
    compileLiteral(expr, dest);
    break;
  case NodeType::LITERAL_LIST:
    compileListLiteral(static_cast<LiteralListNode *>(expr), dest);
    break;
  case NodeType::LITERAL_MAP:
    compileMapLiteral(static_cast<LiteralMapNode *>(expr), dest);
    break;
  case NodeType::IDENTIFIER:
    compileIdentifier(static_cast<IdentifierNode *>(expr), dest);
    break;
  case NodeType::BINARY_OP:
    compileBinaryOp(static_cast<BinaryOpNode *>(expr), dest);
    break;
  case NodeType::UNARY_OP:
    compileUnaryOp(static_cast<UnaryOpNode *>(expr), dest);
    break;
  case NodeType::FUNCTION_CALL:
    compileFunctionCall(static_cast<FunctionCallNode *>(expr), dest);
    break;
  case NodeType::MEMBER_ACCESS:
    compileMemberAccess(static_cast<MemberAccessNode *>(expr), dest);
    break;
  case NodeType::MEMBER_LOOKUP:
    compileMemberLookup(static_cast<MemberLookupNode *>(expr), dest);
    break;
  case NodeType::INDEX_ACCESS:
    compileIndexAccess(static_cast<IndexAccessNode *>(expr), dest);
    break;
  case NodeType::LAMBDA:
    compileLambda(static_cast<LambdaNode *>(expr), dest);
    break;
  case NodeType::NEW_EXPRESSION:
    compileNewExpression(static_cast<NewExpressionNode *>(expr), dest);
    break;
  case NodeType::THIS_EXPRESSION:
    compileThis(static_cast<ThisExpressionNode *>(expr), dest);
    break;
  default:
    error("Unknown expression type", expr->location);
  }
}

void Compiler::compileExpressionForValue(Expression *expr) {
  int slot = cg_->allocSlot();
  compileExpression(expr, slot);
}

void Compiler::compileLiteral(Expression *expr, int dest) {
  ConstantValue val;
  switch (expr->nodeType) {
  case NodeType::LITERAL_INT:
    val = static_cast<LiteralIntNode *>(expr)->value;
    break;
  case NodeType::LITERAL_FLOAT:
    val = static_cast<LiteralFloatNode *>(expr)->value;
    break;
  case NodeType::LITERAL_STRING:
    val = static_cast<LiteralStringNode *>(expr)->value;
    break;
  case NodeType::LITERAL_BOOL: {
    bool b = static_cast<LiteralBoolNode *>(expr)->value;
    cg_->emitABC(OpCode::OP_LOADBOOL, dest, b ? 1 : 0, 0);
    return;
  }
  case NodeType::LITERAL_NULL:
    cg_->emitABC(OpCode::OP_LOADNIL, dest, 0, 0);
    return;
  default:
    return;
  }
  int idx = cg_->addConstant(val);
  cg_->emitABx(OpCode::OP_LOADK, dest, idx);
}

int Compiler::emitLoadEnvironment() {

  int local = cg_->resolveLocal("__env");
  if (local >= 0) {
    int temp = cg_->allocSlot();
    cg_->emitABC(OpCode::OP_MOVE, temp, local, 0);
    return temp;
  }

  int upval = cg_->resolveUpvalue("__env");
  if (upval >= 0) {
    int dest = cg_->allocSlot();
    cg_->emitABC(OpCode::OP_GETUPVAL, dest, upval, 0);
    return dest;
  }

  error("Internal Compiler Error: Global environment '__env' lost in nested "
        "scope.");
  return 0;
}

void Compiler::compileIdentifier(IdentifierNode *node, int dest) {

  int local = cg_->resolveLocal(node->name);
  if (local >= 0) {
    if (local != dest)
      cg_->emitABC(OpCode::OP_MOVE, dest, local, 0);
    return;
  }

  int upval = cg_->resolveUpvalue(node->name);
  if (upval >= 0) {
    cg_->emitABC(OpCode::OP_GETUPVAL, dest, upval, 0);
    return;
  }

  int envSlot = emitLoadEnvironment();

  int nameIdx = cg_->addStringConstant(node->name);

  if (nameIdx <= 255) {
    cg_->emitABC(OpCode::OP_GETFIELD, dest, envSlot, nameIdx);
  } else {
    int keySlot = cg_->allocSlot();
    cg_->emitABx(OpCode::OP_LOADK, keySlot, nameIdx);
    cg_->emitABC(OpCode::OP_GETINDEX, dest, envSlot, keySlot);
    cg_->freeSlots(1);
  }

  cg_->freeSlots(1);
}

static int tryResolveLocalSlot(CodeGen *cg, Expression *expr) {
  if (auto *id = dynamic_cast<IdentifierNode *>(expr)) {
    int local = cg->resolveLocal(id->name);

    if (local >= 0 && local <= 255) {
      return local;
    }
  }
  return -1;
}

void Compiler::compileBinaryOp(BinaryOpNode *node, int dest) {

  if (node->op == OperatorKind::AND) {
    compileExpression(node->left, dest);
    cg_->emitABC(OpCode::OP_TEST, dest, 0, 0);
    int jump = cg_->emitJump(OpCode::OP_JMP);
    compileExpression(node->right, dest);
    cg_->patchJump(jump);
    return;
  }
  if (node->op == OperatorKind::OR) {
    compileExpression(node->left, dest);
    cg_->emitABC(OpCode::OP_TEST, dest, 0, 1);
    int jump = cg_->emitJump(OpCode::OP_JMP);
    compileExpression(node->right, dest);
    cg_->patchJump(jump);
    return;
  }

  if (node->op == OperatorKind::ADD || node->op == OperatorKind::SUB) {
    int8_t imm = 0;
    bool canOptimize = false;

    if (node->op == OperatorKind::ADD) {
      canOptimize = isSmallInt(node->right, imm);
    } else {
      if (isSmallInt(node->right, imm)) {
        if (imm != -128) {
          imm = -imm;
          canOptimize = true;
        }
      }
    }

    if (canOptimize) {

      int leftSlot = tryResolveLocalSlot(cg_.get(), node->left);
      bool isTemp = false;

      if (leftSlot == -1) {

        leftSlot = cg_->allocSlot();
        compileExpression(node->left, leftSlot);
        isTemp = true;
      }

      cg_->emitABC(OpCode::OP_ADDI, dest, leftSlot, static_cast<uint8_t>(imm));

      if (isTemp)
        cg_->freeSlots(1);
      return;
    }
  }

  if (isComparisonOp(node->op)) {

    int leftSlot = tryResolveLocalSlot(cg_.get(), node->left);
    bool isTemp = false;

    if (leftSlot == -1) {
      leftSlot = cg_->allocSlot();
      compileExpression(node->left, leftSlot);
      isTemp = true;
    }

    int8_t imm = 0;
    int constIdx = -1;
    bool optimized = false;
    int k = 0;
    OpCode op = OpCode::OP_EQ;

    bool isEqNe = (node->op == OperatorKind::EQ || node->op == OperatorKind::NE);
    bool isLtGe = (node->op == OperatorKind::LT || node->op == OperatorKind::GE);
    bool isLeGt = (node->op == OperatorKind::LE || node->op == OperatorKind::GT);

    if (node->op == OperatorKind::NE || node->op == OperatorKind::GE ||
        node->op == OperatorKind::GT) {
      k = 1;
    }

    if (isEqNe) {
      if (isSmallInt(node->right, imm)) {
        op = OpCode::OP_EQI;
        optimized = true;
      } else if ((constIdx = tryAddConstantForEQK(cg_.get(), node->right)) != -1) {
        op = OpCode::OP_EQK;
        optimized = true;
      }
    } else if (isLtGe) {
      if (isSmallInt(node->right, imm)) {
        op = OpCode::OP_LTI;
        optimized = true;
      }
    } else if (isLeGt) {
      if (isSmallInt(node->right, imm)) {
        op = OpCode::OP_LEI;
        optimized = true;
      }
    }

    if (optimized) {
      uint8_t bVal =
          (op == OpCode::OP_EQK) ? static_cast<uint8_t>(constIdx) : static_cast<uint8_t>(imm);
      cg_->emitABC(op, leftSlot, bVal, k);
    } else {

      int rightSlot = tryResolveLocalSlot(cg_.get(), node->right);
      bool rightTemp = false;
      if (rightSlot == -1) {
        rightSlot = cg_->allocSlot();
        compileExpression(node->right, rightSlot);
        rightTemp = true;
      }

      OpCode stdOp = OpCode::OP_EQ;
      if (isLtGe)
        stdOp = OpCode::OP_LT;
      else if (isLeGt)
        stdOp = OpCode::OP_LE;

      cg_->emitABC(stdOp, leftSlot, rightSlot, k);
      if (rightTemp)
        cg_->freeSlots(1);
    }

    cg_->emitABC(OpCode::OP_LOADBOOL, dest, 0, 1);
    cg_->emitABC(OpCode::OP_LOADBOOL, dest, 1, 0);

    if (isTemp)
      cg_->freeSlots(1);
    return;
  }

  int leftSlot = tryResolveLocalSlot(cg_.get(), node->left);
  bool leftTemp = false;
  if (leftSlot == -1) {
    leftSlot = cg_->allocSlot();
    compileExpression(node->left, leftSlot);
    leftTemp = true;
  }

  int rightSlot = tryResolveLocalSlot(cg_.get(), node->right);
  bool rightTemp = false;
  if (rightSlot == -1) {
    rightSlot = cg_->allocSlot();
    compileExpression(node->right, rightSlot);
    rightTemp = true;
  }

  OpCode op = binaryOpToOpcode(node->op);
  cg_->emitABC(op, dest, leftSlot, rightSlot);

  if (rightTemp)
    cg_->freeSlots(1);
  if (leftTemp)
    cg_->freeSlots(1);
}

void Compiler::compileUnaryOp(UnaryOpNode *node, int dest) {
  compileExpression(node->operand, dest);
  switch (node->op) {
  case OperatorKind::NEGATE:
    cg_->emitABC(OpCode::OP_UNM, dest, dest, 0);
    break;
  case OperatorKind::NOT:

    cg_->emitABC(OpCode::OP_TEST, dest, 0, 0);
    cg_->emitABC(OpCode::OP_LOADBOOL, dest, 1, 1);
    cg_->emitABC(OpCode::OP_LOADBOOL, dest, 0, 0);
    break;
  default:
    error("Unknown unary operator", node->location);
  }
}

void Compiler::compileMemberAccess(MemberAccessNode *node, int dest) {
  int objSlot = cg_->allocSlot();
  compileExpression(node->objectExpr, objSlot);

  int memberIdx = cg_->addStringConstant(node->memberName);
  if (memberIdx <= 255) {
    cg_->emitABC(OpCode::OP_GETFIELD, dest, objSlot, memberIdx);
  } else {
    int keySlot = cg_->allocSlot();
    cg_->emitABx(OpCode::OP_LOADK, keySlot, memberIdx);
    cg_->emitABC(OpCode::OP_GETINDEX, dest, objSlot, keySlot);
    cg_->freeSlots(1);
  }
  cg_->freeSlots(1);
}

void Compiler::compileMemberLookup(MemberLookupNode *node, int dest) {
  int objSlot = cg_->allocSlot();
  compileExpression(node->objectExpr, objSlot);
  int memberIdx = cg_->addStringConstant(node->memberName);
  cg_->emitABC(OpCode::OP_GETFIELD, dest, objSlot, memberIdx);
  cg_->freeSlots(1);
}

void Compiler::compileIndexAccess(IndexAccessNode *node, int dest) {
  int arrSlot = cg_->allocSlot();
  int idxSlot = cg_->allocSlot();
  compileExpression(node->arrayExpr, arrSlot);
  compileExpression(node->indexExpr, idxSlot);
  cg_->emitABC(OpCode::OP_GETINDEX, dest, arrSlot, idxSlot);
  cg_->freeSlots(2);
}

void Compiler::compileLambda(LambdaNode *node, int dest) { compileLambdaBody(node, dest); }

void Compiler::compileNewExpression(NewExpressionNode *node, int dest) {
  cg_->setLineGetter(node);

  int classSlot = cg_->allocSlot();
  std::string className = node->classType->getFullName();

  int local = cg_->resolveLocal(className);
  if (local >= 0) {
    cg_->emitABC(OpCode::OP_MOVE, classSlot, local, 0);
  } else {
    int upval = cg_->resolveUpvalue(className);
    if (upval >= 0) {
      cg_->emitABC(OpCode::OP_GETUPVAL, classSlot, upval, 0);
    } else {
      int nameIdx = cg_->addStringConstant(className);
      if (nameIdx <= 255) {
        cg_->emitABC(OpCode::OP_GETFIELD, classSlot, 0, nameIdx);
      } else {
        int keySlot = cg_->allocSlot();
        cg_->emitABx(OpCode::OP_LOADK, keySlot, nameIdx);
        cg_->emitABC(OpCode::OP_GETINDEX, classSlot, 0, keySlot);
        cg_->freeSlots(1);
      }
    }
  }

  for (auto *arg : node->arguments) {
    int argSlot = cg_->allocSlot();
    compileExpression(arg, argSlot);
  }

  int argCount = static_cast<int>(node->arguments.size());

  if (argCount > 255) {
    error("Too many arguments for constructor", node->location);
  }
  cg_->emitABC(OpCode::OP_NEWOBJ, dest, classSlot, argCount);
  cg_->freeSlots(1 + argCount);
}

void Compiler::compileThis(ThisExpressionNode *, int dest) {

  int local = cg_->resolveLocal("this");
  if (local >= 0) {
    cg_->emitABC(OpCode::OP_MOVE, dest, local, 0);
    return;
  }

  int upval = cg_->resolveUpvalue("this");
  if (upval >= 0) {
    cg_->emitABC(OpCode::OP_GETUPVAL, dest, upval, 0);
    return;
  }

  error("Use of 'this' without explicit 'this' argument in function signature.");
}

void Compiler::compileListLiteral(LiteralListNode *node, int dest) {
  int capacity = static_cast<int>(node->elements.size());
  cg_->emitABC(OpCode::OP_NEWLIST, dest, capacity > 255 ? 255 : capacity, 0);

  for (size_t i = 0; i < node->elements.size(); ++i) {
    int elemSlot = cg_->allocSlot();
    compileExpression(node->elements[i], elemSlot);

    int idxSlot = cg_->allocSlot();
    int idxConst = cg_->addConstant(static_cast<int64_t>(i));
    cg_->emitABx(OpCode::OP_LOADK, idxSlot, idxConst);
    cg_->emitABC(OpCode::OP_SETINDEX, dest, idxSlot, elemSlot);

    cg_->freeSlots(2);
  }
}

void Compiler::compileMapLiteral(LiteralMapNode *node, int dest) {
  int capacity = static_cast<int>(node->entries.size());
  cg_->emitABC(OpCode::OP_NEWMAP, dest, capacity > 255 ? 255 : capacity, 0);

  for (auto *entry : node->entries) {
    int keySlot = cg_->allocSlot();
    int valSlot = cg_->allocSlot();

    compileExpression(entry->key, keySlot);
    compileExpression(entry->value, valSlot);

    cg_->emitABC(OpCode::OP_SETINDEX, dest, keySlot, valSlot);

    cg_->freeSlots(2);
  }
}

void Compiler::compileLambdaBody(LambdaNode *lambda, int dest) {
  int numParams = static_cast<int>(lambda->params.size());
  cg_->beginFunction(source_, "<lambda>", numParams, lambda->isVariadic, lambda);

  if (!lambda->params.empty() && lambda->params[0]->name == "this") {
    cg_->current()->proto.needsReceiver = true;
  } else {
    cg_->current()->proto.needsReceiver = false;
  }

  int paramIndex = 0;
  for (auto *param : lambda->params) {
    cg_->setLineGetter(param);
    cg_->addLocal(param->name);
    cg_->current()->locals.back().slot = paramIndex++;
    cg_->markInitialized();
  }

  if (lambda->body) {
    for (auto *stmt : lambda->body->statements) {
      compileStatement(stmt);
    }
  }

  cg_->emitABC(OpCode::OP_RETURN, 0, 1, 0);

  Prototype childProto = cg_->endFunction();
  int protoIdx = static_cast<int>(cg_->current()->proto.protos.size());
  cg_->current()->proto.protos.push_back(std::move(childProto));

  cg_->emitABx(OpCode::OP_CLOSURE, dest, protoIdx);
}

void Compiler::emitStoreToEnv(const std::string &name, int srcSlot) {
  int nameIdx = cg_->addStringConstant(name);

  bool isRootFunc = (cg_->current()->enclosing == nullptr);

  if (isRootFunc) {

    if (nameIdx <= 255) {
      cg_->emitABC(OpCode::OP_SETFIELD, 0, nameIdx, srcSlot);
    } else {
      int keySlot = cg_->allocSlot();
      cg_->emitABx(OpCode::OP_LOADK, keySlot, nameIdx);
      cg_->emitABC(OpCode::OP_SETINDEX, 0, keySlot, srcSlot);
      cg_->freeSlots(1);
    }
  } else {

    int envSlot = emitLoadEnvironment();
    if (nameIdx <= 255) {
      cg_->emitABC(OpCode::OP_SETFIELD, envSlot, nameIdx, srcSlot);
    } else {
      int keySlot = cg_->allocSlot();
      cg_->emitABx(OpCode::OP_LOADK, keySlot, nameIdx);
      cg_->emitABC(OpCode::OP_SETINDEX, envSlot, keySlot, srcSlot);
      cg_->freeSlots(1);
    }
    cg_->freeSlots(1);
  }
}

LValue Compiler::compileLValue(Expression *expr) {
  LValue lv;

  cg_->setLineGetter(expr);
  if (auto *id = dynamic_cast<IdentifierNode *>(expr)) {

    int local = cg_->resolveLocal(id->name);
    if (local >= 0) {
      lv.kind = LValue::LOCAL;
      lv.a = local;
      return lv;
    }

    int upval = cg_->resolveUpvalue(id->name);
    if (upval >= 0) {
      lv.kind = LValue::UPVALUE;
      lv.a = upval;
      return lv;
    }

    int nameIdx = cg_->addStringConstant(id->name);
    bool isRootFunc = (cg_->current()->enclosing == nullptr);

    if (isRootFunc && nameIdx <= 255) {
      lv.kind = LValue::GLOBAL;
      lv.a = nameIdx;
    } else {

      int envSlot = emitLoadEnvironment();
      lv.kind = LValue::FIELD;
      lv.a = envSlot;
      lv.b = nameIdx;
    }
    return lv;
  }

  if (auto *member = dynamic_cast<MemberAccessNode *>(expr)) {
    int nameIdx = cg_->addStringConstant(member->memberName);
    if (nameIdx <= 255) {
      lv.kind = LValue::FIELD;
      lv.a = cg_->allocSlot();
      compileExpression(member->objectExpr, lv.a);
      lv.b = nameIdx;
    } else {
      lv.kind = LValue::INDEX;
      lv.a = cg_->allocSlot();
      compileExpression(member->objectExpr, lv.a);
      lv.b = cg_->allocSlot();
      cg_->emitABx(OpCode::OP_LOADK, lv.b, nameIdx);
    }
    return lv;
  }

  if (auto *index = dynamic_cast<IndexAccessNode *>(expr)) {
    lv.kind = LValue::INDEX;
    lv.a = cg_->allocSlot();
    lv.b = cg_->allocSlot();
    compileExpression(index->arrayExpr, lv.a);
    compileExpression(index->indexExpr, lv.b);
    return lv;
  }

  error("Invalid assignment target", expr->location);
  return lv;
}

void Compiler::emitStore(const LValue &lv, int srcReg) {
  switch (lv.kind) {
  case LValue::LOCAL:
    if (lv.a != srcReg)
      cg_->emitABC(OpCode::OP_MOVE, lv.a, srcReg, 0);
    break;
  case LValue::UPVALUE:
    cg_->emitABC(OpCode::OP_SETUPVAL, srcReg, lv.a, 0);
    break;
  case LValue::GLOBAL:
    cg_->emitABC(OpCode::OP_SETFIELD, 0, lv.a, srcReg);
    break;
  case LValue::FIELD:
    cg_->emitABC(OpCode::OP_SETFIELD, lv.a, lv.b, srcReg);
    cg_->freeSlots(1);
    break;
  case LValue::INDEX:
    cg_->emitABC(OpCode::OP_SETINDEX, lv.a, lv.b, srcReg);
    cg_->freeSlots(2);
    break;
  }
}

void Compiler::error(const std::string &msg, const SourceLocation &loc) {
  hasError_ = true;
  CompileError err{msg, loc.filename, loc.line, loc.column};
  errors_.push_back(err);
  if (errorHandler_)
    errorHandler_(err);
}

void Compiler::error(const std::string &msg) { error(msg, {}); }

OpCode Compiler::binaryOpToOpcode(OperatorKind op) {
  switch (op) {
  case OperatorKind::ADD:
  case OperatorKind::ASSIGN_ADD:
    return OpCode::OP_ADD;
  case OperatorKind::SUB:
  case OperatorKind::ASSIGN_SUB:
    return OpCode::OP_SUB;
  case OperatorKind::MUL:
  case OperatorKind::ASSIGN_MUL:
    return OpCode::OP_MUL;
  case OperatorKind::DIV:
  case OperatorKind::ASSIGN_DIV:
    return OpCode::OP_DIV;
  case OperatorKind::MOD:
  case OperatorKind::ASSIGN_MOD:
    return OpCode::OP_MOD;
  default:
    return OpCode::OP_ADD;
  }
}

bool Compiler::isComparisonOp(OperatorKind op) {
  switch (op) {
  case OperatorKind::EQ:
  case OperatorKind::NE:
  case OperatorKind::LT:
  case OperatorKind::LE:
  case OperatorKind::GT:
  case OperatorKind::GE:
    return true;
  default:
    return false;
  }
}

void Compiler::compileImportNamespace(ImportNamespaceNode *node) {

  int moduleNameIdx = cg_->addStringConstant(node->modulePath);

  int destSlot = cg_->addLocal(node->alias);

  cg_->emitABx(OpCode::OP_IMPORT, destSlot, moduleNameIdx);

  if (cg_->currentScopeDepth() == 1) {
    int nameIdx = cg_->addStringConstant(node->alias);
    if (nameIdx <= 255) {
      cg_->emitABC(OpCode::OP_SETFIELD, 0, nameIdx, destSlot);
    } else {
      int keySlot = cg_->allocSlot();
      cg_->emitABx(OpCode::OP_LOADK, keySlot, nameIdx);
      cg_->emitABC(OpCode::OP_SETINDEX, 0, keySlot, destSlot);
      cg_->freeSlots(1);
    }
  }

  cg_->markInitialized();
}

void Compiler::compileImportNamed(ImportNamedNode *node) {

  int moduleNameIdx = cg_->addStringConstant(node->modulePath);

  for (const auto *spec : node->specifiers) {

    if (spec->isTypeOnly) {
      continue;
    }

    int symbolNameIdx = cg_->addStringConstant(spec->importedName);

    std::string localName = spec->getLocalName();
    int destSlot = cg_->addLocal(localName);

    cg_->emitABC(OpCode::OP_IMPORT_FROM, destSlot, moduleNameIdx, symbolNameIdx);

    if (cg_->currentScopeDepth() == 1) {
      int nameIdx = cg_->addStringConstant(localName);
      if (nameIdx <= 255) {
        cg_->emitABC(OpCode::OP_SETFIELD, 0, nameIdx, destSlot);
      } else {
        int keySlot = cg_->allocSlot();
        cg_->emitABx(OpCode::OP_LOADK, keySlot, nameIdx);
        cg_->emitABC(OpCode::OP_SETINDEX, 0, keySlot, destSlot);
        cg_->freeSlots(1);
      }
    }

    cg_->markInitialized();
  }
}

} // namespace spt