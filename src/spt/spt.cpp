#include "spt.h"

#include "../Ast/ast.h"
#include "../Compiler/Compiler.h"
#include "Fiber.h"
#include "GC.h"
#include "Module.h"
#include "Object.h"
#include "StringPool.h"
#include "VM.h"
#include "Value.h"

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct RefEntry {
  spt::Value value;
  bool inUse;
};

struct StateExtra {

  spt_ErrorHandler errorHandler = nullptr;
  void *errorHandlerUserData = nullptr;

  spt_PrintHandler printHandler = nullptr;
  void *printHandlerUserData = nullptr;

  std::string lastError;

  std::vector<RefEntry> refs;
  std::vector<int> freeRefs;

  spt::MapObject *registry = nullptr;

  void *userData = nullptr;

  std::unordered_map<std::string, std::vector<spt_Reg>> cModules;
  std::unordered_map<std::string, int> cModuleRefs;
  spt::Closure *currentCClosure = nullptr;
};

} // namespace

struct spt_State {
  spt::VM *vm;
  spt::FiberObject *fiber;
  StateExtra *extra;
  bool ownsVM;
  spt_State *mainState;
  int callBase = -1;
};

struct spt_Ast {
  AstNode *root;
};

struct spt_Chunk {
  spt::CompiledChunk chunk;
};

struct spt_Compiler {
  std::unique_ptr<spt::Compiler> compiler;
  spt_CompileErrorHandler errorHandler;
  void *errorHandlerUserData;
  std::vector<spt::CompileError> errors;
};

namespace {

inline StateExtra *getExtra(spt_State *S) { return S->mainState->extra; }

inline int absIndex(spt_State *S, int idx) {
  spt::FiberObject *fiber = S->fiber;
  int top = static_cast<int>(fiber->stackTop - fiber->stack);

  if (idx == SPT_REGISTRYINDEX || idx <= SPT_UPVALUEINDEX(1)) {
    return idx;
  }

  if (idx > 0) {
    if (S->callBase >= 0) {
      int result = S->callBase + idx;
      return result;
    }
    return idx;
  } else if (idx < 0) {
    return top + idx + 1;
  }
  return 0;
}

inline spt::Value *getValuePtr(spt_State *S, int idx) {
  spt::FiberObject *fiber = S->fiber;

  if (idx == SPT_REGISTRYINDEX) {
    StateExtra *extra = getExtra(S);
    if (!extra->registry) {
      extra->registry = S->vm->allocateMap(32);
    }
    static thread_local spt::Value regValue;
    regValue = spt::Value::object(extra->registry);
    return &regValue;
  }

  if (idx <= SPT_UPVALUEINDEX(1)) {
    StateExtra *extra = getExtra(S);
    if (!extra->currentCClosure) {
      return nullptr;
    }

    int upvalueIdx = SPT_REGISTRYINDEX - idx;

    int arrayIdx = upvalueIdx;

    if (arrayIdx >= 0 && arrayIdx < extra->currentCClosure->upvalueCount) {
      return &extra->currentCClosure->nativeUpvalues[arrayIdx];
    }
    return nullptr;
  }

  int absIdx = absIndex(S, idx);
  if (absIdx <= 0)
    return nullptr;

  int top = static_cast<int>(fiber->stackTop - fiber->stack);
  if (absIdx > top)
    return nullptr;

  return &fiber->stack[absIdx - 1];
}

inline spt::Value getValue(spt_State *S, int idx) {
  spt::Value *ptr = getValuePtr(S, idx);
  return ptr ? *ptr : spt::Value::nil();
}

inline int valueTypeToSptType(spt::ValueType type) {
  switch (type) {
  case spt::ValueType::Nil:
    return SPT_TNIL;
  case spt::ValueType::Bool:
    return SPT_TBOOL;
  case spt::ValueType::Int:
    return SPT_TINT;
  case spt::ValueType::Float:
    return SPT_TFLOAT;
  case spt::ValueType::String:
    return SPT_TSTRING;
  case spt::ValueType::List:
    return SPT_TLIST;
  case spt::ValueType::Map:
    return SPT_TMAP;
  case spt::ValueType::Object:
    return SPT_TOBJECT;
  case spt::ValueType::Closure:
    return SPT_TCLOSURE;
  case spt::ValueType::Class:
    return SPT_TCLASS;
  case spt::ValueType::Upvalue:
    return SPT_TUPVALUE;
  case spt::ValueType::Fiber:
    return SPT_TFIBER;
  case spt::ValueType::NativeObject:
    return SPT_TCINSTANCE;
  case spt::ValueType::LightUserData:
    return SPT_TLIGHTUSERDATA;
  default:
    return SPT_TNONE;
  }
}

inline void pushValue(spt_State *S, const spt::Value &value) { S->fiber->push(value); }

inline bool ensureStack(spt_State *S, int n) {
  S->fiber->ensureStack(n);
  return true;
}

inline void setError(spt_State *S, const char *msg) {
  StateExtra *extra = getExtra(S);
  extra->lastError = msg ? msg : "";

  if (extra->errorHandler) {
    extra->errorHandler(S, msg, -1, extra->errorHandlerUserData);
  }
}

inline std::string formatString(const char *fmt, va_list ap) {
  va_list ap_copy;
  va_copy(ap_copy, ap);

  int size = std::vsnprintf(nullptr, 0, fmt, ap_copy);
  va_end(ap_copy);

  if (size < 0)
    return "";

  std::string result(size + 1, '\0');
  std::vsnprintf(&result[0], result.size(), fmt, ap);
  result.resize(size);

  return result;
}

int cFunctionTrampoline(spt::VM *vm, spt::Closure *self, int argc, spt::Value *argv) {
  spt_State *S = static_cast<spt_State *>(vm->getUserData());
  if (!S) {
    vm->throwError(spt::Value::object(vm->allocateString("Internal error: no state")));
    return 0;
  }

  StateExtra *extra = getExtra(S);
  spt::Closure *oldClosure = extra->currentCClosure;
  extra->currentCClosure = self;

  if (!self->isNative() || self->upvalueCount < 1)
    return 0;
  spt::Value funcVal = self->getNativeUpvalue(0);
  spt_CFunction cfunc = reinterpret_cast<spt_CFunction>(static_cast<intptr_t>(funcVal.asInt()));

  spt::FiberObject *fiber = S->fiber;
  spt::Value *oldTop = fiber->stackTop;
  int oldFrameCount = fiber->frameCount;
  int oldCallBase = S->callBase;

  bool hasReceiver = !self->receiver.isNil();
  int needed = argc + (hasReceiver ? 1 : 0);
  fiber->ensureStack(needed);

  S->callBase = static_cast<int>(fiber->stackTop - fiber->stack);

  if (hasReceiver) {
    fiber->push(self->receiver);
  }

  for (int i = 0; i < argc; ++i) {
    fiber->push(argv[i]);
  }

  int stackTopAfterPush = static_cast<int>(fiber->stackTop - fiber->stack);

  spt::Value *slot1 = getValuePtr(S, 1);
  if (slot1) {
    if (slot1->isNativeInstance()) {
      spt::NativeInstance *ni = static_cast<spt::NativeInstance *>(slot1->asGC());
    }
  }

  int nResults = 0;

  try {
    nResults = cfunc(S);
  } catch (const spt::SptPanic &e) {
    S->callBase = oldCallBase;
    extra->currentCClosure = oldClosure;
    fiber->stackTop = oldTop;
    fiber->frameCount = oldFrameCount;
    vm->throwError(e.errorValue);
    throw spt::CExtensionException(e.errorValue.toString());
  } catch (const spt::CExtensionException &e) {
    S->callBase = oldCallBase;
    extra->currentCClosure = oldClosure;
    fiber->stackTop = oldTop;
    fiber->frameCount = oldFrameCount;
    throw;
  }

  S->callBase = oldCallBase;
  extra->currentCClosure = oldClosure;

  if (nResults <= 0) {
    fiber->stackTop = oldTop;
    return 0;
  }

  std::vector<spt::Value> results;
  results.reserve(nResults);
  for (int i = 0; i < nResults; ++i) {
    int peekIdx = nResults - 1 - i;
    spt::Value val = fiber->peek(peekIdx);
    results.push_back(val);
  }

  fiber->stackTop = oldTop;

  for (int i = 0; i < nResults; ++i) {
    fiber->push(results[i]);
  }

  return nResults;
}

spt::Closure *createCClosure(spt_State *S, spt_CFunction fn, int nupvalues) {

  int totalUpvalues = nupvalues + 1;

  spt::Closure *closure = S->vm->gc().allocateNativeClosure(totalUpvalues);
  closure->function = cFunctionTrampoline;
  closure->arity = -1;
  closure->receiver = spt::Value::nil();

  closure->setNativeUpvalue(0, spt::Value::integer(reinterpret_cast<intptr_t>(fn)));

  return closure;
}

} // namespace

extern "C" {

SPT_API spt_State *spt_newstate(void) { return spt_newstateex(0, 0, true); }

SPT_API spt_State *spt_newstateex(size_t stackSize, size_t heapSize, bool enableGC) {
  try {
    spt::VMConfig config;
    if (stackSize > 0)
      config.stackSize = stackSize;
    if (heapSize > 0)
      config.heapSize = heapSize;
    config.enableGC = enableGC;

    spt_State *S = new spt_State();
    S->vm = new spt::VM(config);
    S->fiber = S->vm->mainFiber();
    S->extra = new StateExtra();
    S->ownsVM = true;
    S->mainState = S;
    S->callBase = -1;

    S->vm->setUserData(S);

    S->vm->gc().addRoot([S](spt::Value &) {
      StateExtra *extra = S->extra;
      if (!extra)
        return;

      for (auto &entry : extra->refs) {
        if (entry.inUse && !entry.value.isNil()) {
          if (entry.value.type != spt::ValueType::Bool && entry.value.type != spt::ValueType::Int &&
              entry.value.type != spt::ValueType::Float &&
              entry.value.type != spt::ValueType::LightUserData) {
            S->vm->gc().markObject(entry.value.asGC());
          }
        }
      }

      if (extra->registry) {
        S->vm->gc().markObject(extra->registry);
      }
    });

    S->vm->setErrorHandler([](const std::string &msg, int line) {

    });

    S->vm->setPrintHandler([](const std::string &msg) {
      std::fputs(msg.c_str(), stdout);
      std::fputc('\n', stdout);
    });

    return S;

  } catch (const std::exception &e) {
    return nullptr;
  } catch (...) {
    return nullptr;
  }
}

SPT_API void spt_close(spt_State *S) {
  if (!S)
    return;

  if (S->ownsVM) {
    delete S->extra;
    delete S->vm;
  }

  delete S;
}

SPT_API spt_State *spt_getcurrent(spt_State *S) {
  if (!S)
    return nullptr;

  if (S->fiber == S->vm->currentFiber()) {
    return S;
  }

  spt_State *current = new spt_State();
  current->vm = S->vm;
  current->fiber = S->vm->currentFiber();
  current->extra = nullptr;
  current->ownsVM = false;
  current->mainState = S->mainState;

  return current;
}

SPT_API spt_State *spt_getmain(spt_State *S) { return S ? S->mainState : nullptr; }

SPT_API void spt_setuserdata(spt_State *S, void *ud) {
  if (S) {
    getExtra(S)->userData = ud;
  }
}

SPT_API void *spt_getuserdata(spt_State *S) { return S ? getExtra(S)->userData : nullptr; }

SPT_API int spt_gettop(spt_State *S) {
  if (!S || !S->fiber)
    return 0;

  spt::FiberObject *fiber = S->fiber;
  int absoluteTop = static_cast<int>(fiber->stackTop - fiber->stack);

  if (S->callBase >= 0) {
    int relativeTop = absoluteTop - S->callBase;
    return relativeTop;
  }
  return absoluteTop;
}

SPT_API void spt_settop(spt_State *S, int idx) {
  if (!S)
    return;

  spt::FiberObject *fiber = S->fiber;
  int top = static_cast<int>(fiber->stackTop - fiber->stack);

  int newTop;
  if (idx >= 0) {
    newTop = idx;
  } else {
    newTop = top + idx + 1;
    if (newTop < 0)
      newTop = 0;
  }

  if (newTop > top) {

    fiber->ensureStack(newTop - top);
    while (fiber->stackTop - fiber->stack < newTop) {
      fiber->push(spt::Value::nil());
    }
  } else if (newTop < top) {

    fiber->stackTop = fiber->stack + newTop;
  }
}

SPT_API void spt_pushvalue(spt_State *S, int idx) {
  if (!S)
    return;
  pushValue(S, getValue(S, idx));
}

SPT_API void spt_rotate(spt_State *S, int idx, int n) {
  if (!S)
    return;

  int absIdx = absIndex(S, idx);
  if (absIdx <= 0)
    return;

  spt::FiberObject *fiber = S->fiber;
  int top = static_cast<int>(fiber->stackTop - fiber->stack);

  if (absIdx > top)
    return;

  spt::Value *start = &fiber->stack[absIdx - 1];
  spt::Value *end = fiber->stackTop;
  int count = static_cast<int>(end - start);

  if (count <= 1 || n == 0)
    return;

  n = n % count;
  if (n < 0)
    n += count;

  std::reverse(start, end);
  std::reverse(start, start + n);
  std::reverse(start + n, end);
}

SPT_API void spt_copy(spt_State *S, int fromidx, int toidx) {
  if (!S)
    return;

  spt::Value *from = getValuePtr(S, fromidx);
  spt::Value *to = getValuePtr(S, toidx);

  if (from && to) {
    *to = *from;
  }
}

SPT_API void spt_insert(spt_State *S, int idx) {
  if (!S)
    return;
  spt_rotate(S, idx, 1);
}

SPT_API void spt_remove(spt_State *S, int idx) {
  if (!S)
    return;
  spt_rotate(S, idx, -1);
  spt_settop(S, -2);
}

SPT_API void spt_replace(spt_State *S, int idx) {
  if (!S)
    return;
  spt_copy(S, -1, idx);
  spt_settop(S, -2);
}

SPT_API int spt_checkstack(spt_State *S, int n) {
  if (!S || n < 0)
    return 0;
  return ensureStack(S, n) ? 1 : 0;
}

SPT_API void spt_xmove(spt_State *from, spt_State *to, int n) {
  if (!from || !to || n <= 0)
    return;
  if (from == to)
    return;

  int fromStackDepth = static_cast<int>(from->fiber->stackTop - from->fiber->stack);
  if (n > fromStackDepth) {

    n = fromStackDepth;
    if (n <= 0)
      return;
  }

  to->fiber->ensureStack(n);

  for (int i = n; i > 0; --i) {
    to->fiber->push(from->fiber->peek(i - 1));
  }

  from->fiber->stackTop -= n;
}

SPT_API int spt_absindex(spt_State *S, int idx) { return absIndex(S, idx); }

SPT_API void spt_pushnil(spt_State *S) {
  if (S)
    pushValue(S, spt::Value::nil());
}

SPT_API void spt_pushbool(spt_State *S, int b) {
  if (S)
    pushValue(S, spt::Value::boolean(b != 0));
}

SPT_API void spt_pushint(spt_State *S, spt_Int n) {
  if (S)
    pushValue(S, spt::Value::integer(n));
}

SPT_API void spt_pushfloat(spt_State *S, spt_Float n) {
  if (S)
    pushValue(S, spt::Value::number(n));
}

SPT_API void spt_pushstring(spt_State *S, const char *s) {
  if (!S)
    return;
  if (!s) {
    spt_pushnil(S);
    return;
  }
  spt::StringObject *str = S->vm->allocateString(s);
  pushValue(S, spt::Value::object(str));
}

SPT_API void spt_pushlstring(spt_State *S, const char *s, size_t len) {
  if (!S)
    return;
  if (!s) {
    spt_pushnil(S);
    return;
  }
  spt::StringObject *str = S->vm->allocateString(std::string_view(s, len));
  pushValue(S, spt::Value::object(str));
}

SPT_API const char *spt_pushfstring(spt_State *S, const char *fmt, ...) {
  if (!S || !fmt)
    return nullptr;

  va_list ap;
  va_start(ap, fmt);
  const char *result = spt_pushvfstring(S, fmt, ap);
  va_end(ap);

  return result;
}

SPT_API const char *spt_pushvfstring(spt_State *S, const char *fmt, va_list ap) {
  if (!S || !fmt)
    return nullptr;

  std::string str = formatString(fmt, ap);
  spt::StringObject *strObj = S->vm->allocateString(str);
  pushValue(S, spt::Value::object(strObj));

  return strObj->c_str();
}

SPT_API void spt_pushlightuserdata(spt_State *S, void *p) {
  if (!S)
    return;

  pushValue(S, spt::Value::lightUserData(p));
}

SPT_API int spt_type(spt_State *S, int idx) {
  if (!S)
    return SPT_TNONE;

  spt::Value *v = getValuePtr(S, idx);
  if (!v)
    return SPT_TNONE;

  return valueTypeToSptType(v->type);
}

SPT_API const char *spt_typename(spt_State *S, int tp) {
  (void)S;
  switch (tp) {
  case SPT_TNONE:
    return "no value";
  case SPT_TNIL:
    return "nil";
  case SPT_TBOOL:
    return "bool";
  case SPT_TINT:
    return "int";
  case SPT_TFLOAT:
    return "float";
  case SPT_TSTRING:
    return "string";
  case SPT_TLIST:
    return "list";
  case SPT_TMAP:
    return "map";
  case SPT_TOBJECT:
    return "object";
  case SPT_TCLOSURE:
    return "function";
  case SPT_TCLASS:
    return "class";
  case SPT_TUPVALUE:
    return "upvalue";
  case SPT_TFIBER:
    return "fiber";
  case SPT_TCINSTANCE:
    return "cinstance";
  case SPT_TLIGHTUSERDATA:
    return "lightuserdata";
  default:
    return "unknown";
  }
}

SPT_API int spt_isbool(spt_State *S, int idx) { return spt_type(S, idx) == SPT_TBOOL; }

SPT_API int spt_isint(spt_State *S, int idx) { return spt_type(S, idx) == SPT_TINT; }

SPT_API int spt_isfloat(spt_State *S, int idx) { return spt_type(S, idx) == SPT_TFLOAT; }

SPT_API int spt_isnumber(spt_State *S, int idx) {
  int tp = spt_type(S, idx);
  return tp == SPT_TINT || tp == SPT_TFLOAT;
}

SPT_API int spt_isstring(spt_State *S, int idx) { return spt_type(S, idx) == SPT_TSTRING; }

SPT_API int spt_islist(spt_State *S, int idx) { return spt_type(S, idx) == SPT_TLIST; }

SPT_API int spt_ismap(spt_State *S, int idx) { return spt_type(S, idx) == SPT_TMAP; }

SPT_API int spt_isfunction(spt_State *S, int idx) { return spt_type(S, idx) == SPT_TCLOSURE; }

SPT_API int spt_iscfunction(spt_State *S, int idx) {
  if (!S)
    return 0;
  spt::Value v = getValue(S, idx);
  if (!v.isClosure())
    return 0;
  spt::Closure *closure = static_cast<spt::Closure *>(v.asGC());
  return closure->isNative() ? 1 : 0;
}

SPT_API int spt_isclass(spt_State *S, int idx) { return spt_type(S, idx) == SPT_TCLASS; }

SPT_API int spt_isobject(spt_State *S, int idx) { return spt_type(S, idx) == SPT_TOBJECT; }

SPT_API int spt_iscinstance(spt_State *S, int idx) { return spt_type(S, idx) == SPT_TCINSTANCE; }

SPT_API int spt_isfiber(spt_State *S, int idx) { return spt_type(S, idx) == SPT_TFIBER; }

SPT_API int spt_islightuserdata(spt_State *S, int idx) {
  return spt_type(S, idx) == SPT_TLIGHTUSERDATA;
}

SPT_API int spt_toboolean(spt_State *S, int idx) {
  if (!S)
    return 0;
  return getValue(S, idx).isTruthy() ? 1 : 0;
}

SPT_API int spt_tobool(spt_State *S, int idx) {
  if (!S)
    return 0;
  spt::Value v = getValue(S, idx);
  return v.isBool() ? (v.asBool() ? 1 : 0) : 0;
}

SPT_API spt_Int spt_toint(spt_State *S, int idx) { return spt_tointx(S, idx, nullptr); }

SPT_API spt_Int spt_tointx(spt_State *S, int idx, int *isnum) {
  if (!S) {
    if (isnum)
      *isnum = 0;
    return 0;
  }

  spt::Value v = getValue(S, idx);

  if (v.isInt()) {
    if (isnum)
      *isnum = 1;
    return v.asInt();
  }

  if (v.isFloat()) {
    if (isnum)
      *isnum = 1;
    return static_cast<spt_Int>(v.asFloat());
  }

  if (isnum)
    *isnum = 0;
  return 0;
}

SPT_API spt_Float spt_tofloat(spt_State *S, int idx) { return spt_tofloatx(S, idx, nullptr); }

SPT_API spt_Float spt_tofloatx(spt_State *S, int idx, int *isnum) {
  if (!S) {
    if (isnum)
      *isnum = 0;
    return 0.0;
  }

  spt::Value v = getValue(S, idx);

  if (v.isFloat()) {
    if (isnum)
      *isnum = 1;
    return v.asFloat();
  }

  if (v.isInt()) {
    if (isnum)
      *isnum = 1;
    return static_cast<spt_Float>(v.asInt());
  }

  if (isnum)
    *isnum = 0;
  return 0.0;
}

SPT_API const char *spt_tostring(spt_State *S, int idx, size_t *len) {
  if (!S) {
    if (len)
      *len = 0;
    return nullptr;
  }

  spt::Value v = getValue(S, idx);
  if (!v.isString()) {
    if (len)
      *len = 0;
    return nullptr;
  }

  spt::StringObject *str = v.asString();
  if (len)
    *len = str->length;
  return str->c_str();
}

SPT_API void *spt_tocinstance(spt_State *S, int idx) {
  if (!S)
    return nullptr;

  spt::Value v = getValue(S, idx);
  if (!v.isNativeInstance())
    return nullptr;

  spt::NativeInstance *inst = static_cast<spt::NativeInstance *>(v.asGC());
  return inst->data;
}

SPT_API spt_State *spt_tofiber(spt_State *S, int idx) {
  if (!S)
    return nullptr;

  spt::Value v = getValue(S, idx);
  if (!v.isFiber())
    return nullptr;

  spt::FiberObject *fiber = static_cast<spt::FiberObject *>(v.asGC());

  spt_State *fiberState = new spt_State();
  fiberState->vm = S->vm;
  fiberState->fiber = fiber;
  fiberState->extra = nullptr;
  fiberState->ownsVM = false;
  fiberState->mainState = S->mainState;

  return fiberState;
}

SPT_API const void *spt_topointer(spt_State *S, int idx) {
  if (!S)
    return nullptr;

  spt::Value v = getValue(S, idx);
  if (v.isNil())
    return nullptr;

  switch (v.type) {
  case spt::ValueType::String:
  case spt::ValueType::List:
  case spt::ValueType::Map:
  case spt::ValueType::Object:
  case spt::ValueType::Closure:
  case spt::ValueType::Class:
  case spt::ValueType::Fiber:
  case spt::ValueType::NativeObject:
    return v.asGC();
  case spt::ValueType::LightUserData:
    return v.asLightUserData();
  default:
    return nullptr;
  }
}

SPT_API void *spt_tolightuserdata(spt_State *S, int idx) {
  if (!S)
    return nullptr;

  spt::Value v = getValue(S, idx);
  if (!v.isLightUserData())
    return nullptr;

  return v.asLightUserData();
}

SPT_API int spt_compare(spt_State *S, int idx1, int idx2) {
  if (!S)
    return 0;

  spt::Value a = getValue(S, idx1);
  spt::Value b = getValue(S, idx2);

  if (a.type == b.type) {
    switch (a.type) {
    case spt::ValueType::Int:
      if (a.asInt() < b.asInt())
        return -1;
      if (a.asInt() > b.asInt())
        return 1;
      return 0;
    case spt::ValueType::Float:
      if (a.asFloat() < b.asFloat())
        return -1;
      if (a.asFloat() > b.asFloat())
        return 1;
      return 0;
    case spt::ValueType::String:
      return std::strcmp(a.asString()->c_str(), b.asString()->c_str());
    case spt::ValueType::LightUserData: {
      void *pa = a.asLightUserData();
      void *pb = b.asLightUserData();
      if (pa == pb)
        return 0;
      return pa < pb ? -1 : 1;
    }
    default:
      return a.equals(b) ? 0 : (a.asGC() < b.asGC() ? -1 : 1);
    }
  }

  if (a.isNumber() && b.isNumber()) {
    double da = a.asNumber();
    double db = b.asNumber();
    if (da < db)
      return -1;
    if (da > db)
      return 1;
    return 0;
  }

  return static_cast<int>(a.type) - static_cast<int>(b.type);
}

SPT_API int spt_equal(spt_State *S, int idx1, int idx2) {
  if (!S)
    return 0;
  return getValue(S, idx1).equals(getValue(S, idx2)) ? 1 : 0;
}

SPT_API int spt_rawequal(spt_State *S, int idx1, int idx2) {
  if (!S)
    return 0;

  spt::Value a = getValue(S, idx1);
  spt::Value b = getValue(S, idx2);

  if (a.type != b.type)
    return 0;

  switch (a.type) {
  case spt::ValueType::Nil:
    return 1;
  case spt::ValueType::Bool:
    return a.asBool() == b.asBool() ? 1 : 0;
  case spt::ValueType::Int:
    return a.asInt() == b.asInt() ? 1 : 0;
  case spt::ValueType::Float:
    return a.asFloat() == b.asFloat() ? 1 : 0;
  case spt::ValueType::LightUserData:
    return a.asLightUserData() == b.asLightUserData() ? 1 : 0;
  default:
    return a.asGC() == b.asGC() ? 1 : 0;
  }
}

SPT_API void spt_newlist(spt_State *S, int capacity) {
  if (!S)
    return;
  spt::ListObject *list = S->vm->allocateList(capacity);
  pushValue(S, spt::Value::object(list));
}

SPT_API int spt_listlen(spt_State *S, int idx) {
  if (!S)
    return 0;

  spt::Value v = getValue(S, idx);
  if (!v.isList())
    return 0;

  spt::ListObject *list = static_cast<spt::ListObject *>(v.asGC());
  return static_cast<int>(list->elements.size());
}

SPT_API void spt_listappend(spt_State *S, int idx) {
  if (!S)
    return;

  spt::Value *listPtr = getValuePtr(S, idx);
  if (!listPtr || !listPtr->isList())
    return;

  spt::ListObject *list = static_cast<spt::ListObject *>(listPtr->asGC());
  spt::Value value = S->fiber->pop();
  list->elements.push_back(value);
}

SPT_API void spt_listgeti(spt_State *S, int idx, int n) {
  if (!S)
    return;

  spt::Value v = getValue(S, idx);
  if (!v.isList()) {
    pushValue(S, spt::Value::nil());
    return;
  }

  spt::ListObject *list = static_cast<spt::ListObject *>(v.asGC());
  if (n < 0 || n >= static_cast<int>(list->elements.size())) {
    pushValue(S, spt::Value::nil());
    return;
  }

  pushValue(S, list->elements[n]);
}

SPT_API void spt_listseti(spt_State *S, int idx, int n) {
  if (!S)
    return;

  spt::Value *listPtr = getValuePtr(S, idx);
  if (!listPtr || !listPtr->isList())
    return;

  spt::ListObject *list = static_cast<spt::ListObject *>(listPtr->asGC());
  spt::Value value = S->fiber->pop();

  if (n < 0 || n >= static_cast<int>(list->elements.size()))
    return;
  list->elements[n] = value;
}

SPT_API void spt_listinsert(spt_State *S, int idx, int n) {
  if (!S)
    return;

  spt::Value *listPtr = getValuePtr(S, idx);
  if (!listPtr || !listPtr->isList())
    return;

  spt::ListObject *list = static_cast<spt::ListObject *>(listPtr->asGC());
  spt::Value value = S->fiber->pop();

  if (n < 0)
    n = 0;
  if (n > static_cast<int>(list->elements.size())) {
    n = static_cast<int>(list->elements.size());
  }

  list->elements.insert(list->elements.begin() + n, value);
}

SPT_API void spt_listremove(spt_State *S, int idx, int n) {
  if (!S)
    return;

  spt::Value *listPtr = getValuePtr(S, idx);
  if (!listPtr || !listPtr->isList()) {
    pushValue(S, spt::Value::nil());
    return;
  }

  spt::ListObject *list = static_cast<spt::ListObject *>(listPtr->asGC());
  if (n < 0 || n >= static_cast<int>(list->elements.size())) {
    pushValue(S, spt::Value::nil());
    return;
  }

  spt::Value removed = list->elements[n];
  list->elements.erase(list->elements.begin() + n);
  pushValue(S, removed);
}

SPT_API void spt_listclear(spt_State *S, int idx) {
  if (!S)
    return;

  spt::Value *listPtr = getValuePtr(S, idx);
  if (!listPtr || !listPtr->isList())
    return;

  spt::ListObject *list = static_cast<spt::ListObject *>(listPtr->asGC());
  list->elements.clear();
}

SPT_API void spt_newmap(spt_State *S, int capacity) {
  if (!S)
    return;
  spt::MapObject *map = S->vm->allocateMap(capacity);
  pushValue(S, spt::Value::object(map));
}

SPT_API int spt_maplen(spt_State *S, int idx) {
  if (!S)
    return 0;

  spt::Value v = getValue(S, idx);
  if (!v.isMap())
    return 0;

  spt::MapObject *map = static_cast<spt::MapObject *>(v.asGC());
  return static_cast<int>(map->entries.size());
}

SPT_API int spt_getmap(spt_State *S, int idx) {
  if (!S)
    return SPT_TNIL;

  spt::Value v = getValue(S, idx);
  if (!v.isMap()) {
    S->fiber->pop();
    pushValue(S, spt::Value::nil());
    return SPT_TNIL;
  }

  spt::MapObject *map = static_cast<spt::MapObject *>(v.asGC());
  spt::Value key = S->fiber->pop();
  spt::Value result = map->get(key);
  pushValue(S, result);

  return valueTypeToSptType(result.type);
}

SPT_API void spt_setmap(spt_State *S, int idx) {
  if (!S)
    return;

  spt::Value *mapPtr = getValuePtr(S, idx);
  if (!mapPtr || !mapPtr->isMap()) {
    S->fiber->pop();
    S->fiber->pop();
    return;
  }

  spt::MapObject *map = static_cast<spt::MapObject *>(mapPtr->asGC());
  spt::Value value = S->fiber->pop();
  spt::Value key = S->fiber->pop();
  map->set(key, value);
}

SPT_API int spt_getfield(spt_State *S, int idx, const char *key) {
  if (!S || !key)
    return SPT_TNIL;

  spt::Value v = getValue(S, idx);
  if (!v.isMap()) {
    pushValue(S, spt::Value::nil());
    return SPT_TNIL;
  }

  spt::MapObject *map = static_cast<spt::MapObject *>(v.asGC());
  spt::StringObject *keyStr = S->vm->allocateString(key);
  spt::Value result = map->get(spt::Value::object(keyStr));
  pushValue(S, result);

  return valueTypeToSptType(result.type);
}

SPT_API void spt_setfield(spt_State *S, int idx, const char *key) {
  if (!S || !key)
    return;

  spt::Value *mapPtr = getValuePtr(S, idx);
  if (!mapPtr || !mapPtr->isMap()) {
    S->fiber->pop();
    return;
  }

  spt::MapObject *map = static_cast<spt::MapObject *>(mapPtr->asGC());
  spt::StringObject *keyStr = S->vm->allocateString(key);
  spt::Value value = S->fiber->pop();
  map->set(spt::Value::object(keyStr), value);
}

SPT_API int spt_haskey(spt_State *S, int idx) {
  if (!S)
    return 0;

  spt::Value v = getValue(S, idx);
  if (!v.isMap()) {
    S->fiber->pop();
    return 0;
  }

  spt::MapObject *map = static_cast<spt::MapObject *>(v.asGC());
  spt::Value key = S->fiber->pop();

  return map->has(key) ? 1 : 0;
}

SPT_API void spt_mapremove(spt_State *S, int idx) {
  if (!S)
    return;

  spt::Value *mapPtr = getValuePtr(S, idx);
  if (!mapPtr || !mapPtr->isMap()) {
    S->fiber->pop();
    pushValue(S, spt::Value::nil());
    return;
  }

  spt::MapObject *map = static_cast<spt::MapObject *>(mapPtr->asGC());
  spt::Value key = S->fiber->pop();

  spt::Value value = map->get(key);

  auto it = map->entries.find(key);
  if (it != map->entries.end()) {
    map->entries.erase(it);
  }

  pushValue(S, value);
}

SPT_API void spt_mapclear(spt_State *S, int idx) {
  if (!S)
    return;

  spt::Value *mapPtr = getValuePtr(S, idx);
  if (!mapPtr || !mapPtr->isMap())
    return;

  spt::MapObject *map = static_cast<spt::MapObject *>(mapPtr->asGC());
  map->entries.clear();
}

SPT_API void spt_mapkeys(spt_State *S, int idx) {
  if (!S)
    return;

  spt::Value v = getValue(S, idx);
  if (!v.isMap()) {
    spt_newlist(S, 0);
    return;
  }

  spt::MapObject *map = static_cast<spt::MapObject *>(v.asGC());
  spt::ListObject *keys = S->vm->allocateList(0);

  for (const auto &[key, val] : map->entries) {
    keys->elements.push_back(key);
  }

  pushValue(S, spt::Value::object(keys));
}

SPT_API void spt_mapvalues(spt_State *S, int idx) {
  if (!S)
    return;

  spt::Value v = getValue(S, idx);
  if (!v.isMap()) {
    spt_newlist(S, 0);
    return;
  }

  spt::MapObject *map = static_cast<spt::MapObject *>(v.asGC());
  spt::ListObject *values = S->vm->allocateList(0);

  for (const auto &[key, val] : map->entries) {
    values->elements.push_back(val);
  }

  pushValue(S, spt::Value::object(values));
}

SPT_API int spt_mapnext(spt_State *S, int idx) {
  if (!S)
    return 0;

  spt::Value v = getValue(S, idx);
  if (!v.isMap()) {
    S->fiber->pop();
    return 0;
  }

  spt::MapObject *map = static_cast<spt::MapObject *>(v.asGC());
  spt::Value prevKey = S->fiber->pop();

  if (map->entries.empty()) {
    return 0;
  }

  auto it = map->entries.begin();

  if (!prevKey.isNil()) {

    auto findIt = map->entries.find(prevKey);
    if (findIt != map->entries.end()) {
      ++findIt;
      it = findIt;
    }
  }

  if (it == map->entries.end()) {
    return 0;
  }

  pushValue(S, it->first);
  pushValue(S, it->second);
  return 1;
}

SPT_API void spt_newclass(spt_State *S, const char *name) {
  if (!S)
    return;
  spt::ClassObject *klass = S->vm->allocateClass(name ? name : "");
  pushValue(S, spt::Value::object(klass));
}

SPT_API void spt_bindmethod(spt_State *S, int class_idx, const char *name) {
  if (!S || !name)
    return;

  spt::Value *classPtr = getValuePtr(S, class_idx);
  if (!classPtr || !classPtr->isClass()) {
    S->fiber->pop();
    return;
  }

  spt::ClassObject *klass = static_cast<spt::ClassObject *>(classPtr->asGC());
  spt::Value method = S->fiber->pop();
  spt::StringObject *nameStr = S->vm->allocateString(name);
  klass->setMethod(nameStr, method);
}

SPT_API void spt_bindstatic(spt_State *S, int class_idx, const char *name) {
  if (!S || !name)
    return;

  spt::Value *classPtr = getValuePtr(S, class_idx);
  if (!classPtr || !classPtr->isClass()) {
    S->fiber->pop();
    return;
  }

  spt::ClassObject *klass = static_cast<spt::ClassObject *>(classPtr->asGC());
  spt::Value value = S->fiber->pop();
  spt::StringObject *nameStr = S->vm->allocateString(name);
  klass->statics[nameStr] = value;
}

SPT_API void spt_newinstance(spt_State *S, int nargs) {
  if (!S)
    return;

  int classIdx = -(nargs + 1);
  spt::Value classVal = getValue(S, classIdx);

  if (!classVal.isClass()) {
    setError(S, "Cannot instantiate non-class value");
    spt_settop(S, classIdx - 1);
    pushValue(S, spt::Value::nil());
    return;
  }

  spt::ClassObject *klass = static_cast<spt::ClassObject *>(classVal.asGC());
  spt::Instance *instance = S->vm->allocateInstance(klass);

  spt::StringObject *initName = S->vm->allocateString("__init");
  if (spt::Value *initMethod = klass->methods.get(initName)) {
  }

  spt_settop(S, classIdx - 1);
  pushValue(S, spt::Value::object(instance));
}

SPT_API int spt_getprop(spt_State *S, int obj_idx, const char *name) {
  if (!S || !name)
    return SPT_TNIL;

  spt::Value v = getValue(S, obj_idx);
  spt::StringObject *nameStr = S->vm->allocateString(name);

  if (v.isInstance()) {
    spt::Instance *inst = static_cast<spt::Instance *>(v.asGC());

    if (spt::Value *field = inst->fields.get(nameStr)) {
      pushValue(S, *field);
      return valueTypeToSptType(field->type);
    }

    if (inst->klass) {
      if (spt::Value *method = inst->klass->methods.get(nameStr)) {
        pushValue(S, *method);
        return valueTypeToSptType(method->type);
      }
    }
  } else if (v.isClass()) {
    spt::ClassObject *klass = static_cast<spt::ClassObject *>(v.asGC());

    if (spt::Value *staticVal = klass->statics.get(nameStr)) {
      pushValue(S, *staticVal);
      return valueTypeToSptType(staticVal->type);
    }
  } else if (v.isNativeInstance()) {
    spt::NativeInstance *inst = static_cast<spt::NativeInstance *>(v.asGC());

    if (spt::Value *field = inst->fields.get(nameStr)) {
      pushValue(S, *field);
      return valueTypeToSptType(field->type);
    }

    if (inst->klass) {
      if (spt::Value *method = inst->klass->methods.get(nameStr)) {
        pushValue(S, *method);
        return valueTypeToSptType(method->type);
      }
    }
  }

  pushValue(S, spt::Value::nil());
  return SPT_TNIL;
}

SPT_API void spt_setprop(spt_State *S, int obj_idx, const char *name) {
  if (!S || !name)
    return;

  spt::Value *objPtr = getValuePtr(S, obj_idx);
  spt::Value value = S->fiber->pop();

  if (!objPtr)
    return;

  spt::StringObject *nameStr = S->vm->allocateString(name);

  if (objPtr->isInstance()) {
    spt::Instance *inst = static_cast<spt::Instance *>(objPtr->asGC());
    inst->setField(nameStr, value);
  } else if (objPtr->isNativeInstance()) {
    spt::NativeInstance *inst = static_cast<spt::NativeInstance *>(objPtr->asGC());
    inst->setField(nameStr, value);
  }
}

SPT_API int spt_hasprop(spt_State *S, int obj_idx, const char *name) {
  if (!S || !name)
    return 0;

  spt::Value v = getValue(S, obj_idx);
  spt::StringObject *nameStr = S->vm->allocateString(name);

  if (v.isInstance()) {
    spt::Instance *inst = static_cast<spt::Instance *>(v.asGC());
    if (inst->hasField(nameStr))
      return 1;
    if (inst->klass && inst->klass->methods.contains(nameStr))
      return 1;
  } else if (v.isNativeInstance()) {
    spt::NativeInstance *inst = static_cast<spt::NativeInstance *>(v.asGC());
    if (inst->hasField(nameStr))
      return 1;
    if (inst->klass && inst->klass->methods.contains(nameStr))
      return 1;
  }

  return 0;
}

SPT_API int spt_getclass(spt_State *S, int obj_idx) {
  if (!S)
    return SPT_TNIL;

  spt::Value v = getValue(S, obj_idx);

  if (v.isInstance()) {
    spt::Instance *inst = static_cast<spt::Instance *>(v.asGC());
    if (inst->klass) {
      pushValue(S, spt::Value::object(inst->klass));
      return SPT_TCLASS;
    }
  } else if (v.isNativeInstance()) {
    spt::NativeInstance *inst = static_cast<spt::NativeInstance *>(v.asGC());
    if (inst->klass) {
      pushValue(S, spt::Value::object(inst->klass));
      return SPT_TCLASS;
    }
  }

  pushValue(S, spt::Value::nil());
  return SPT_TNIL;
}

SPT_API const char *spt_classname(spt_State *S, int class_idx) {
  if (!S)
    return nullptr;

  spt::Value v = getValue(S, class_idx);
  if (!v.isClass())
    return nullptr;

  spt::ClassObject *klass = static_cast<spt::ClassObject *>(v.asGC());
  return klass->name.c_str();
}

SPT_API int spt_isinstance(spt_State *S, int obj_idx, int class_idx) {
  if (!S)
    return 0;

  spt::Value objVal = getValue(S, obj_idx);
  spt::Value classVal = getValue(S, class_idx);

  if (!classVal.isClass())
    return 0;
  spt::ClassObject *targetClass = static_cast<spt::ClassObject *>(classVal.asGC());

  if (objVal.isInstance()) {
    spt::Instance *inst = static_cast<spt::Instance *>(objVal.asGC());
    return inst->klass == targetClass ? 1 : 0;
  } else if (objVal.isNativeInstance()) {
    spt::NativeInstance *inst = static_cast<spt::NativeInstance *>(objVal.asGC());
    return inst->klass == targetClass ? 1 : 0;
  }

  return 0;
}

SPT_API void *spt_newcinstance(spt_State *S, size_t size) {
  if (!S)
    return nullptr;

  void *data = size > 0 ? std::malloc(size) : nullptr;
  spt::NativeInstance *inst = S->vm->allocateNativeInstance(nullptr, data);
  pushValue(S, spt::Value::object(inst));

  return data;
}

SPT_API void *spt_newcinstanceof(spt_State *S, size_t size) {
  if (!S)
    return nullptr;

  spt::Value classVal = S->fiber->pop();
  if (!classVal.isClass()) {
    pushValue(S, spt::Value::nil());
    return nullptr;
  }

  spt::ClassObject *klass = static_cast<spt::ClassObject *>(classVal.asGC());
  void *data = size > 0 ? std::malloc(size) : nullptr;
  spt::NativeInstance *inst = S->vm->allocateNativeInstance(klass, data);
  pushValue(S, spt::Value::object(inst));

  return data;
}

SPT_API void spt_setcclass(spt_State *S, int cinst_idx) {
  if (!S)
    return;

  spt::Value *instPtr = getValuePtr(S, cinst_idx);
  if (!instPtr || !instPtr->isNativeInstance()) {
    S->fiber->pop();
    return;
  }

  spt::Value classVal = S->fiber->pop();
  if (!classVal.isClass())
    return;

  spt::NativeInstance *inst = static_cast<spt::NativeInstance *>(instPtr->asGC());
  inst->klass = static_cast<spt::ClassObject *>(classVal.asGC());
}

SPT_API void *spt_getcinstancedata(spt_State *S, int idx) { return spt_tocinstance(S, idx); }

SPT_API int spt_iscinstancevalid(spt_State *S, int idx) {
  if (!S)
    return 0;

  spt::Value v = getValue(S, idx);
  if (!v.isNativeInstance())
    return 0;

  spt::NativeInstance *inst = static_cast<spt::NativeInstance *>(v.asGC());
  return inst->isValid() && !inst->isFinalized ? 1 : 0;
}

namespace {

static const char *const kMagicMethodNames[] = {
    "__init", "__gc",   "__get", "__set",  "__getitem", "__setitem", "__add", "__sub",
    "__mul",  "__div",  "__mod", "__pow",  "__unm",     "__idiv",    "__eq",  "__lt",
    "__le",   "__band", "__bor", "__bxor", "__bnot",    "__shl",     "__shr",
};

static_assert(sizeof(kMagicMethodNames) / sizeof(kMagicMethodNames[0]) == SPT_MM_MAX,
              "kMagicMethodNames size must match SPT_MM_MAX");

inline spt::ClassObject *getClassObject(spt_State *S, int idx) {
  spt::Value v = getValue(S, idx);
  if (!v.isClass())
    return nullptr;
  return static_cast<spt::ClassObject *>(v.asGC());
}

inline spt::ClassObject *getInstanceClass(spt_State *S, int idx) {
  spt::Value v = getValue(S, idx);
  if (v.isInstance()) {
    return static_cast<spt::Instance *>(v.asGC())->klass;
  }
  if (v.isNativeInstance()) {
    return static_cast<spt::NativeInstance *>(v.asGC())->klass;
  }
  return nullptr;
}

} // namespace

SPT_API const char *spt_magicmethodname(int mm) {
  if (mm < 0 || mm >= SPT_MM_MAX)
    return nullptr;
  return kMagicMethodNames[mm];
}

SPT_API int spt_magicmethodindex(const char *name) {
  if (!name)
    return SPT_MM_MAX;

  if (name[0] != '_' || name[1] != '_')
    return SPT_MM_MAX;

  for (int i = 0; i < SPT_MM_MAX; ++i) {
    if (std::strcmp(name, kMagicMethodNames[i]) == 0)
      return i;
  }
  return SPT_MM_MAX;
}

SPT_API unsigned int spt_getclassflags(spt_State *S, int class_idx) {
  if (!S)
    return 0;

  spt::ClassObject *klass = getClassObject(S, class_idx);
  if (!klass)
    return 0;

  return klass->flags;
}

SPT_API int spt_hasmagicmethod(spt_State *S, int class_idx, int mm) {
  if (!S || mm < 0 || mm >= SPT_MM_MAX)
    return 0;

  spt::ClassObject *klass = getClassObject(S, class_idx);
  if (!klass)
    return 0;

  return klass->hasFlag(static_cast<uint32_t>(1u << mm)) ? 1 : 0;
}

SPT_API int spt_getmagicmethod(spt_State *S, int class_idx, int mm) {
  if (!S || mm < 0 || mm >= SPT_MM_MAX) {
    if (S)
      pushValue(S, spt::Value::nil());
    return SPT_TNIL;
  }

  spt::ClassObject *klass = getClassObject(S, class_idx);
  if (!klass) {
    pushValue(S, spt::Value::nil());
    return SPT_TNIL;
  }

  spt::Value method = klass->getMagicMethod(static_cast<spt::MagicMethod>(mm));
  pushValue(S, method);
  return valueTypeToSptType(method.type);
}

SPT_API void spt_setmagicmethod(spt_State *S, int class_idx, int mm) {
  if (!S || mm < 0 || mm >= SPT_MM_MAX) {
    if (S)
      S->fiber->pop();
    return;
  }

  spt::ClassObject *klass = getClassObject(S, class_idx);
  if (!klass) {
    S->fiber->pop();
    return;
  }

  spt::Value method = S->fiber->pop();

  klass->setMagicMethodDirect(static_cast<spt::MagicMethod>(mm), method);

  spt::StringObject *nameStr = S->vm->allocateString(kMagicMethodNames[mm]);
  klass->methods[nameStr] = method;
}

SPT_API void spt_setmagicmethodbyname(spt_State *S, int class_idx, const char *name) {
  if (!S || !name) {
    if (S)
      S->fiber->pop();
    return;
  }

  spt::ClassObject *klass = getClassObject(S, class_idx);
  if (!klass) {
    S->fiber->pop();
    return;
  }

  spt::Value method = S->fiber->pop();
  spt::StringObject *nameStr = S->vm->allocateString(name);

  klass->setMethod(nameStr, method);
}

SPT_API int spt_objhasmagicmethod(spt_State *S, int obj_idx, int mm) {
  if (!S || mm < 0 || mm >= SPT_MM_MAX)
    return 0;

  spt::ClassObject *klass = getInstanceClass(S, obj_idx);
  if (!klass)
    return 0;

  return klass->hasFlag(static_cast<uint32_t>(1u << mm)) ? 1 : 0;
}

SPT_API int spt_objgetmagicmethod(spt_State *S, int obj_idx, int mm) {
  if (!S || mm < 0 || mm >= SPT_MM_MAX) {
    if (S)
      pushValue(S, spt::Value::nil());
    return SPT_TNIL;
  }

  spt::ClassObject *klass = getInstanceClass(S, obj_idx);
  if (!klass) {
    pushValue(S, spt::Value::nil());
    return SPT_TNIL;
  }

  spt::Value method = klass->getMagicMethod(static_cast<spt::MagicMethod>(mm));
  pushValue(S, method);
  return valueTypeToSptType(method.type);
}

SPT_API int spt_callmagicmethod(spt_State *S, int mm, int nargs, int nresults) {
  if (!S || mm < 0 || mm >= SPT_MM_MAX)
    return SPT_ERRRUN;

  int objIdx = -(nargs + 1);
  spt::ClassObject *klass = getInstanceClass(S, objIdx);
  if (!klass) {
    setError(S, "Cannot call magic method on non-object");
    return SPT_ERRRUN;
  }

  if (!klass->hasFlag(static_cast<uint32_t>(1u << mm))) {
    const char *methodName = kMagicMethodNames[mm];
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Object does not have magic method '%s'", methodName);
    setError(S, buf);
    return SPT_ERRRUN;
  }

  spt::Value methodVal = klass->getMagicMethod(static_cast<spt::MagicMethod>(mm));
  if (!methodVal.isClosure()) {
    setError(S, "Magic method is not a callable");
    return SPT_ERRRUN;
  }

  spt::Closure *closure = static_cast<spt::Closure *>(methodVal.asGC());

  pushValue(S, methodVal);

  spt_rotate(S, objIdx, 1);

  return spt_call(S, nargs + 1, nresults);
}

SPT_API int spt_pcallmagicmethod(spt_State *S, int mm, int nargs, int nresults, int errfunc) {
  if (!S || mm < 0 || mm >= SPT_MM_MAX)
    return SPT_ERRRUN;

  int objIdx = -(nargs + 1);
  spt::ClassObject *klass = getInstanceClass(S, objIdx);
  if (!klass) {
    spt_pushstring(S, "Cannot call magic method on non-object");
    return SPT_ERRRUN;
  }

  if (!klass->hasFlag(static_cast<uint32_t>(1u << mm))) {
    const char *methodName = kMagicMethodNames[mm];
    spt_pushfstring(S, "Object does not have magic method '%s'", methodName);
    return SPT_ERRRUN;
  }

  spt::Value methodVal = klass->getMagicMethod(static_cast<spt::MagicMethod>(mm));
  if (!methodVal.isClosure()) {
    spt_pushstring(S, "Magic method is not a callable");
    return SPT_ERRRUN;
  }

  pushValue(S, methodVal);
  spt_rotate(S, objIdx, 1);

  return spt_pcall(S, nargs + 1, nresults, errfunc);
}

SPT_API void spt_pushcclosure(spt_State *S, spt_CFunction fn, int nup) {
  if (!S || !fn)
    return;

  spt::Closure *closure = createCClosure(S, fn, nup);

  for (int i = nup; i > 0; --i) {
    spt::Value upval = S->fiber->pop();
    closure->setNativeUpvalue(i, upval);
  }

  pushValue(S, spt::Value::object(closure));
}

SPT_API void spt_getupvalue(spt_State *S, int func_idx, int n) {
  if (!S || n < 1) {
    pushValue(S, spt::Value::nil());
    return;
  }

  spt::Value v = getValue(S, func_idx);
  if (!v.isClosure()) {
    pushValue(S, spt::Value::nil());
    return;
  }

  spt::Closure *closure = static_cast<spt::Closure *>(v.asGC());

  if (closure->isNative()) {

    if (n > closure->upvalueCount - 1) {
      pushValue(S, spt::Value::nil());
      return;
    }
    pushValue(S, closure->getNativeUpvalue(n));
  } else {

    if (n > closure->upvalueCount) {
      pushValue(S, spt::Value::nil());
      return;
    }
    spt::UpValue *uv = closure->getScriptUpvalue(n - 1);
    pushValue(S, uv ? *uv->location : spt::Value::nil());
  }
}

SPT_API void spt_setupvalue(spt_State *S, int func_idx, int n) {
  if (!S || n < 1) {
    S->fiber->pop();
    return;
  }

  spt::Value *funcPtr = getValuePtr(S, func_idx);
  if (!funcPtr || !funcPtr->isClosure()) {
    S->fiber->pop();
    return;
  }

  spt::Closure *closure = static_cast<spt::Closure *>(funcPtr->asGC());
  spt::Value value = S->fiber->pop();

  if (closure->isNative()) {
    if (n <= closure->upvalueCount - 1) {
      closure->setNativeUpvalue(n, value);
    }
  } else {
    if (n <= closure->upvalueCount) {
      spt::UpValue *uv = closure->getScriptUpvalue(n - 1);
      if (uv)
        *uv->location = value;
    }
  }
}

SPT_API int spt_getupvaluecount(spt_State *S, int func_idx) {
  if (!S)
    return 0;

  spt::Value v = getValue(S, func_idx);
  if (!v.isClosure())
    return 0;

  spt::Closure *closure = static_cast<spt::Closure *>(v.asGC());

  if (closure->isNative()) {
    return closure->upvalueCount > 0 ? closure->upvalueCount - 1 : 0;
  } else {
    return closure->upvalueCount;
  }
}

SPT_API int spt_getarity(spt_State *S, int func_idx) {
  if (!S)
    return 0;

  spt::Value v = getValue(S, func_idx);
  if (!v.isClosure())
    return 0;

  spt::Closure *closure = static_cast<spt::Closure *>(v.asGC());

  if (closure->isNative()) {
    return closure->arity;
  } else {
    return closure->proto ? closure->proto->numParams : 0;
  }
}

SPT_API spt_Ast *spt_parse(const char *source, const char *filename) {
  AstNode *ast = loadAst(source ? source : "", filename ? filename : "<string>");
  if (!ast)
    return nullptr;

  spt_Ast *result = new spt_Ast();
  result->root = ast;
  return result;
}

SPT_API spt_Ast *spt_parsefile(const char *filename) {
  if (!filename)
    return nullptr;
  return spt_parse(nullptr, filename);
}

SPT_API void spt_freeast(spt_Ast *ast) {
  if (ast) {
    destroyAst(ast->root);
    delete ast;
  }
}

SPT_API spt_Compiler *spt_newcompiler(const char *moduleName, const char *source) {
  spt_Compiler *compiler = new spt_Compiler();
  compiler->compiler = std::make_unique<spt::Compiler>(moduleName ? moduleName : "main",
                                                       source ? source : "<unknown>");
  compiler->errorHandler = nullptr;
  compiler->errorHandlerUserData = nullptr;

  compiler->compiler->setErrorHandler([compiler](const spt::CompileError &err) {
    compiler->errors.push_back(err);
    if (compiler->errorHandler) {
      compiler->errorHandler(err.message.c_str(), err.line, err.column, err.filename.c_str(),
                             compiler->errorHandlerUserData);
    }
  });

  return compiler;
}

SPT_API void spt_freecompiler(spt_Compiler *compiler) { delete compiler; }

SPT_API void spt_setcompileerrorhandler(spt_Compiler *compiler, spt_CompileErrorHandler handler,
                                        void *ud) {
  if (compiler) {
    compiler->errorHandler = handler;
    compiler->errorHandlerUserData = ud;
  }
}

SPT_API spt_Chunk *spt_compile(spt_Compiler *compiler, spt_Ast *ast) {
  if (!compiler || !ast || !ast->root)
    return nullptr;

  compiler->errors.clear();

  spt::CompiledChunk chunk = compiler->compiler->compile(ast->root);

  if (compiler->compiler->hasError()) {
    return nullptr;
  }

  spt_Chunk *result = new spt_Chunk();
  result->chunk = std::move(chunk);
  return result;
}

SPT_API int spt_compilerhaserror(spt_Compiler *compiler) {
  return (compiler && compiler->compiler->hasError()) ? 1 : 0;
}

SPT_API int spt_compilererrorcount(spt_Compiler *compiler) {
  return compiler ? static_cast<int>(compiler->errors.size()) : 0;
}

SPT_API int spt_compilergeterror(spt_Compiler *compiler, int index, const char **message, int *line,
                                 int *column) {
  if (!compiler || index < 0 || index >= static_cast<int>(compiler->errors.size())) {
    return 0;
  }

  const spt::CompileError &err = compiler->errors[index];
  if (message)
    *message = err.message.c_str();
  if (line)
    *line = err.line;
  if (column)
    *column = err.column;

  return 1;
}

SPT_API void spt_freechunk(spt_Chunk *chunk) { delete chunk; }

SPT_API spt_Chunk *spt_loadstring(spt_State *S, const char *source, const char *name) {
  if (!S || !source)
    return nullptr;

  spt_Ast *ast = spt_parse(source, name);
  if (!ast) {
    setError(S, "Parse error");
    return nullptr;
  }

  spt_Compiler *compiler = spt_newcompiler(name, name);
  spt_Chunk *chunk = spt_compile(compiler, ast);

  if (!chunk) {
    if (compiler->errors.size() > 0) {
      setError(S, compiler->errors[0].message.c_str());
    } else {
      setError(S, "Compilation error");
    }
  }

  spt_freecompiler(compiler);
  spt_freeast(ast);

  return chunk;
}

SPT_API spt_Chunk *spt_loadfile(spt_State *S, const char *filename) {
  return spt_loadstring(S, nullptr, filename);
}

SPT_API void spt_pushchunk(spt_State *S, spt_Chunk *chunk) {
  if (!S || !chunk) {
    if (S)
      pushValue(S, spt::Value::nil());
    return;
  }

  S->vm->prepareChunk(chunk->chunk);
  spt::Closure *closure = S->vm->allocateScriptClosure(&chunk->chunk.mainProto);
  pushValue(S, spt::Value::object(closure));
}

SPT_API int spt_execute(spt_State *S, spt_Chunk *chunk) {
  if (!S || !chunk)
    return SPT_ERRRUN;

  spt::InterpretResult result;
  try {
    result = S->vm->interpret(chunk->chunk);
  } catch (const spt::SptPanic &e) {
    S->vm->clearError();
    return SPT_ERRRUN;
  } catch (const spt::CExtensionException &e) {
    S->vm->clearError();
    return SPT_ERRRUN;
  }

  switch (result) {
  case spt::InterpretResult::OK:
    return SPT_OK;
  case spt::InterpretResult::COMPILE_ERROR:
    return SPT_ERRCOMPILE;
  case spt::InterpretResult::RUNTIME_ERROR:
    return SPT_ERRRUN;
  default:
    return SPT_ERRRUN;
  }
}

SPT_API int spt_call(spt_State *S, int nargs, int nresults) {
  if (!S)
    return SPT_ERRRUN;

  spt::FiberObject *fiber = S->fiber;

  int topBefore = static_cast<int>(fiber->stackTop - fiber->stack);
  int baseHeight = topBefore - nargs - 1;

  int funcIdx = -(nargs + 1);
  spt::Value funcVal = getValue(S, funcIdx);

  if (!funcVal.isClosure()) {
    setError(S, "Attempt to call non-function value");
    spt_pushstring(S, "Attempt to call non-function value");
    return SPT_ERRRUN;
  }

  spt::Closure *closure = static_cast<spt::Closure *>(funcVal.asGC());

  if (!ensureStack(S, nargs + 1)) {
    setError(S, "Stack overflow");
    spt_pushstring(S, "Stack overflow");
    return SPT_ERRRUN;
  }

  spt::InterpretResult result;
  try {
    result = S->vm->call(closure, nargs, nresults);
  } catch (const spt::SptPanic &e) {
    fiber->stackTop = fiber->stack + baseHeight;
    pushValue(S, e.errorValue);
    S->vm->clearError();
    return SPT_ERRRUN;
  } catch (const spt::CExtensionException &e) {
    fiber->stackTop = fiber->stack + baseHeight;
    pushValue(S, spt::Value::object(S->vm->allocateString(e.what())));
    S->vm->clearError();
    return SPT_ERRRUN;
  }

  if (result != spt::InterpretResult::OK) {
    if (S->vm->hasError()) {
      spt::Value err = S->vm->getErrorValue();

      fiber->stackTop = fiber->stack + baseHeight;
      pushValue(S, err);
      S->vm->clearError();
    } else {
      fiber->stackTop = fiber->stack + baseHeight;
      spt_pushstring(S, "Runtime error during call");
    }
    return SPT_ERRRUN;
  }

  return SPT_OK;
}

SPT_API int spt_pcall(spt_State *S, int nargs, int nresults, int errfunc) {
  if (!S)
    return SPT_ERRRUN;

  (void)errfunc;

  int funcIdx = spt_gettop(S) - nargs - 1;

  try {
    int status = spt_call(S, nargs, nresults);
    return status;
  } catch (const spt::SptPanic &e) {
    spt_settop(S, funcIdx);
    if (e.errorValue.isString()) {
      spt_pushstring(S, e.errorValue.asString()->c_str());
    } else {
      spt_pushstring(S, e.errorValue.toString().c_str());
    }
    return SPT_ERRRUN;
  } catch (const spt::CExtensionException &e) {
    spt_settop(S, funcIdx);
    spt_pushstring(S, e.what());
    return SPT_ERRRUN;
  } catch (const std::exception &e) {
    spt_settop(S, funcIdx);
    spt_pushstring(S, e.what());
    return SPT_ERRRUN;
  } catch (...) {
    spt_settop(S, funcIdx);
    spt_pushstring(S, "Unknown error");
    return SPT_ERRERR;
  }
}

SPT_API int spt_callmethod(spt_State *S, const char *method, int nargs, int nresults) {
  if (!S || !method)
    return SPT_ERRRUN;

  int objIdx = -(nargs + 1);
  spt::Value objVal = getValue(S, objIdx);

  spt::StringObject *methodName = S->vm->allocateString(method);
  spt::Value methodVal = spt::Value::nil();

  if (objVal.isInstance()) {
    spt::Instance *inst = static_cast<spt::Instance *>(objVal.asGC());
    if (inst->klass) {
      if (spt::Value *m = inst->klass->methods.get(methodName)) {
        methodVal = *m;
      }
    }
  } else if (objVal.isNativeInstance()) {
    spt::NativeInstance *inst = static_cast<spt::NativeInstance *>(objVal.asGC());
    if (inst->klass) {
      if (spt::Value *m = inst->klass->methods.get(methodName)) {
        methodVal = *m;
      }
    }
  } else if (objVal.isClass()) {
    spt::ClassObject *klass = static_cast<spt::ClassObject *>(objVal.asGC());
    if (spt::Value *m = klass->statics.get(methodName)) {
      methodVal = *m;
    }
  }

  if (methodVal.isNil() || !methodVal.isClosure()) {

    spt_settop(S, objIdx - 1);
    spt_pushfstring(S, "Method '%s' not found", method);
    return SPT_ERRRUN;
  }

  spt::Closure *closure = static_cast<spt::Closure *>(methodVal.asGC());
  if (closure->isNative()) {
    closure->receiver = objVal;
  }

  spt::Value *objPtr = getValuePtr(S, objIdx);
  *objPtr = methodVal;

  return spt_call(S, nargs, nresults);
}

SPT_API int spt_pcallmethod(spt_State *S, const char *method, int nargs, int nresults,
                            int errfunc) {
  if (!S || !method)
    return SPT_ERRRUN;

  (void)errfunc;
  int funcIdx = spt_gettop(S) - nargs - 1;

  try {

    int status = spt_callmethod(S, method, nargs, nresults);

    if (status != SPT_OK) {

      spt::Value errVal = S->fiber->pop();
      spt_settop(S, funcIdx);
      pushValue(S, errVal);
      return status;
    }
    return SPT_OK;
  } catch (...) {
    spt_settop(S, funcIdx);
    spt_pushstring(S, "Method call error");
    return SPT_ERRRUN;
  }
}

SPT_API int spt_dostring(spt_State *S, const char *source, const char *name) {
  if (!S || !source)
    return SPT_ERRRUN;

  spt_Chunk *chunk = spt_loadstring(S, source, name);
  if (!chunk)
    return SPT_ERRCOMPILE;

  int result = spt_execute(S, chunk);
  spt_freechunk(chunk);

  return result;
}

SPT_API int spt_dofile(spt_State *S, const char *filename) {
  if (!S || !filename)
    return SPT_ERRFILE;

  spt_Chunk *chunk = spt_loadfile(S, filename);
  if (!chunk)
    return SPT_ERRCOMPILE;

  int result = spt_execute(S, chunk);
  spt_freechunk(chunk);

  return result;
}

SPT_API spt_State *spt_newfiber(spt_State *S) {
  if (!S)
    return nullptr;

  spt::Value funcVal = S->fiber->pop();
  if (!funcVal.isClosure()) {
    pushValue(S, spt::Value::nil());
    return nullptr;
  }

  spt::Closure *closure = static_cast<spt::Closure *>(funcVal.asGC());
  spt::FiberObject *fiber = S->vm->allocateFiber(closure);

  pushValue(S, spt::Value::object(fiber));

  spt_State *fiberState = new spt_State();
  fiberState->vm = S->vm;
  fiberState->fiber = fiber;
  fiberState->extra = nullptr;
  fiberState->ownsVM = false;
  fiberState->mainState = S->mainState;

  return fiberState;
}

SPT_API int spt_resume(spt_State *S, spt_State *from, int nargs) {
  if (!S || !from || !S->fiber)
    return SPT_ERRRUN;

  spt::FiberObject *fiber = S->fiber;

  if (!fiber->canResume()) {
    return fiber->isError() ? SPT_ERRRUN : SPT_OK;
  }

  spt::Value arg = spt::Value::nil();
  if (nargs > 0) {
    arg = from->fiber->pop();
  }

  spt::Value result = spt::Value::nil();
  try {
    result = S->vm->fiberCall(fiber, arg, false);
  } catch (const spt::SptPanic &e) {

    return SPT_ERRRUN;
  } catch (const spt::CExtensionException &e) {

    return SPT_ERRRUN;
  }

  from->fiber->push(result);

  if (fiber->isError())
    return SPT_ERRRUN;
  if (fiber->isSuspended())
    return SPT_YIELD;
  return SPT_OK;
}

SPT_API int spt_yield(spt_State *S, int nresults) {
  if (!S)
    return 0;

  spt::Value value = spt::Value::nil();
  if (nresults > 0) {
    value = S->fiber->pop();
  }

  S->vm->fiberYield(value);

  return 0;
}

SPT_API int spt_fiberstatus(spt_State *S) {
  if (!S || !S->fiber)
    return SPT_FIBER_ERROR;

  switch (S->fiber->state) {
  case spt::FiberState::NEW:
    return SPT_FIBER_NEW;
  case spt::FiberState::RUNNING:
    return SPT_FIBER_RUNNING;
  case spt::FiberState::SUSPENDED:
    return SPT_FIBER_SUSPENDED;
  case spt::FiberState::DONE:
    return SPT_FIBER_DONE;
  case spt::FiberState::ERROR:
    return SPT_FIBER_ERROR;
  default:
    return SPT_FIBER_ERROR;
  }
}

SPT_API int spt_isresumable(spt_State *S) {
  return (S && S->fiber && S->fiber->canResume()) ? 1 : 0;
}

SPT_API void spt_fiberabort(spt_State *S) {
  if (!S)
    return;

  spt::Value error = S->fiber->pop();
  S->vm->fiberAbort(error);
}

SPT_API void spt_fibererror(spt_State *S) {
  if (!S || !S->fiber) {
    if (S)
      pushValue(S, spt::Value::nil());
    return;
  }

  if (S->fiber->hasError) {
    pushValue(S, S->fiber->error);
  } else {
    pushValue(S, spt::Value::nil());
  }
}

SPT_API int spt_getglobal(spt_State *S, const char *name) {
  if (!S || !name) {
    if (S)
      pushValue(S, spt::Value::nil());
    return SPT_TNIL;
  }

  spt::Value value = S->vm->getGlobal(name);
  pushValue(S, value);
  return valueTypeToSptType(value.type);
}

SPT_API void spt_setglobal(spt_State *S, const char *name) {
  if (!S || !name) {
    if (S)
      S->fiber->pop();
    return;
  }

  spt::Value value = S->fiber->pop();
  S->vm->setGlobal(name, value);
}

SPT_API int spt_hasglobal(spt_State *S, const char *name) {
  if (!S || !name)
    return 0;

  spt::Value value = S->vm->getGlobal(name);
  return value.isNil() ? 0 : 1;
}

SPT_API int spt_ref(spt_State *S) {
  if (!S)
    return SPT_NOREF;

  StateExtra *extra = getExtra(S);
  spt::Value value = S->fiber->pop();

  if (value.isNil())
    return SPT_REFNIL;

  int ref;
  if (!extra->freeRefs.empty()) {
    ref = extra->freeRefs.back();
    extra->freeRefs.pop_back();
    extra->refs[ref] = {value, true};
  } else {
    ref = static_cast<int>(extra->refs.size());
    extra->refs.push_back({value, true});
  }

  return ref;
}

SPT_API void spt_unref(spt_State *S, int ref) {
  if (!S || ref < 0)
    return;

  StateExtra *extra = getExtra(S);
  if (ref >= static_cast<int>(extra->refs.size()))
    return;

  if (extra->refs[ref].inUse) {
    extra->refs[ref].inUse = false;
    extra->refs[ref].value = spt::Value::nil();
    extra->freeRefs.push_back(ref);
  }
}

SPT_API void spt_getref(spt_State *S, int ref) {
  if (!S)
    return;

  if (ref == SPT_REFNIL || ref == SPT_NOREF || ref < 0) {
    pushValue(S, spt::Value::nil());
    return;
  }

  StateExtra *extra = getExtra(S);
  if (ref >= static_cast<int>(extra->refs.size()) || !extra->refs[ref].inUse) {
    pushValue(S, spt::Value::nil());
    return;
  }

  pushValue(S, extra->refs[ref].value);
}

SPT_API void spt_addpath(spt_State *S, const char *path) {
  if (!S || !path)
    return;

  spt::ModuleManager *mm = S->vm->moduleManager();
  if (!mm)
    return;

  spt::ModuleLoader *loader = mm->getLoader();
  if (loader) {
    spt::FileSystemLoader *fsLoader = reinterpret_cast<spt::FileSystemLoader *>(loader);
    FileSystemLoader_addSearchPath(fsLoader, path);
  }
}

SPT_API int spt_import(spt_State *S, const char *name) {
  if (!S || !name)
    return SPT_ERRRUN;

  StateExtra *extra = getExtra(S);
  auto it = extra->cModuleRefs.find(name);
  if (it != extra->cModuleRefs.end()) {
    spt_getref(S, it->second);
    return SPT_OK;
  }

  spt::ModuleManager *mm = S->vm->moduleManager();
  if (!mm) {
    setError(S, "Module manager not initialized");
    return SPT_ERRRUN;
  }

  spt::Value result = mm->loadModule(name);

  if (result.isMap()) {
    spt::MapObject *map = static_cast<spt::MapObject *>(result.asGC());

    spt::StringObject *errorKey = S->vm->allocateString("error");
    spt::Value errorVal = map->get(spt::Value::object(errorKey));
    if (errorVal.isBool() && errorVal.asBool()) {
      spt::StringObject *msgKey = S->vm->allocateString("message");
      spt::Value msgVal = map->get(spt::Value::object(msgKey));
      if (msgVal.isString()) {
        setError(S, msgVal.asString()->c_str());
      }
      return SPT_ERRRUN;
    }
  }

  pushValue(S, result);
  return SPT_OK;
}

SPT_API int spt_reload(spt_State *S, const char *name) {
  if (!S || !name)
    return SPT_ERRRUN;

  spt::ModuleManager *mm = S->vm->moduleManager();
  if (!mm)
    return SPT_ERRRUN;

  return mm->reloadModule(name) ? SPT_OK : SPT_ERRRUN;
}

SPT_API void spt_defmodule(spt_State *S, const char *name, const spt_Reg *funcs) {
  if (!S || !name || !funcs)
    return;

  StateExtra *extra = getExtra(S);
  std::vector<spt_Reg> funcList;

  for (const spt_Reg *f = funcs; f->name != nullptr; ++f) {
    funcList.push_back(*f);
  }

  extra->cModules[name] = funcList;

  spt::MapObject *exportsTable = S->vm->allocateMap(static_cast<int>(funcList.size()));
  S->vm->protect(spt::Value::object(exportsTable));

  for (const auto &f : funcList) {
    spt::Closure *closure = createCClosure(S, f.func, 0);
    closure->arity = f.arity;
    closure->name = S->vm->allocateString(f.name);

    spt::StringObject *key = S->vm->allocateString(f.name);
    exportsTable->set(spt::Value::object(key), spt::Value::object(closure));
  }

  S->vm->unprotect(1);

  S->fiber->push(spt::Value::object(exportsTable));
  int ref = spt_ref(S);
  extra->cModuleRefs[name] = ref;
}

SPT_API void spt_tickmodules(spt_State *S) {
  if (!S)
    return;

  spt::ModuleManager *mm = S->vm->moduleManager();
  if (mm) {
    mm->checkForUpdates();
  }
}

SPT_API void spt_registermodule(spt_State *S, const char *name, spt_Chunk *chunk) {
  if (!S || !name || !chunk)
    return;

  spt::CompiledChunk chunkCopy;
  chunkCopy.mainProto = chunk->chunk.mainProto.deepCopy();
  chunkCopy.exports = chunk->chunk.exports;

  S->vm->registerModule(name, std::move(chunkCopy));
}

SPT_API void spt_error(spt_State *S, const char *fmt, ...) {
  if (!S)
    return;

  char buffer[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  throw spt::CExtensionException(std::string(buffer));
}

SPT_API void spt_throw(spt_State *S) {
  if (!S)
    return;

  spt::Value error = S->fiber->pop();

  spt::VM *vm = S->vm;

  vm->throwPanic(error);
}

SPT_API void spt_seterrorhandler(spt_State *S, spt_ErrorHandler handler, void *ud) {
  if (!S)
    return;

  StateExtra *extra = getExtra(S);
  extra->errorHandler = handler;
  extra->errorHandlerUserData = ud;
}

SPT_API void spt_setprinthandler(spt_State *S, spt_PrintHandler handler, void *ud) {
  if (!S)
    return;

  StateExtra *extra = getExtra(S);
  extra->printHandler = handler;
  extra->printHandlerUserData = ud;

  S->vm->setPrintHandler([extra](const std::string &msg) {
    if (extra->printHandler) {
      extra->printHandler(nullptr, msg.c_str(), extra->printHandlerUserData);
    } else {
      std::fputs(msg.c_str(), stdout);
      std::fputc('\n', stdout);
    }
  });
}

SPT_API const char *spt_getlasterror(spt_State *S) {
  if (!S)
    return nullptr;

  StateExtra *extra = getExtra(S);
  return extra->lastError.empty() ? nullptr : extra->lastError.c_str();
}

SPT_API void spt_stacktrace(spt_State *S) {
  if (!S)
    return;

  std::string trace;
  spt::FiberObject *fiber = S->fiber;

  for (int i = fiber->frameCount - 1; i >= 0; --i) {
    const spt::CallFrame &frame = fiber->frames[i];
    if (!frame.closure)
      continue;

    const char *name = frame.closure->getName();
    trace += "  at ";
    trace += name;

    if (frame.closure->isScript() && frame.closure->proto) {
      trace += " (";
      trace += frame.closure->proto->source;
      trace += ")";
    }

    trace += "\n";
  }

  spt::StringObject *traceStr = S->vm->allocateString(trace);
  pushValue(S, spt::Value::object(traceStr));
}

SPT_API int spt_getinfo(spt_State *S, int func_idx, const char *what, const char **name,
                        const char **source, int *lineDefined, int *currentLine) {
  if (!S || !what)
    return 0;

  spt::Value v = getValue(S, func_idx);
  if (!v.isClosure())
    return 0;

  spt::Closure *closure = static_cast<spt::Closure *>(v.asGC());

  while (*what) {
    switch (*what++) {
    case 'S':
      if (closure->isScript() && closure->proto) {
        if (name)
          *name = closure->proto->name.c_str();
        if (source)
          *source = closure->proto->source.c_str();
        if (lineDefined)
          *lineDefined = closure->proto->lineDefined;
      } else {
        if (name)
          *name = closure->name ? closure->name->c_str() : "<native>";
        if (source)
          *source = "<native>";
        if (lineDefined)
          *lineDefined = -1;
      }
      break;
    case 'l':
      if (currentLine)
        *currentLine = -1;
      break;
    }
  }

  return 1;
}

SPT_API int spt_getstack(spt_State *S, int level, const char *what, const char **name,
                         const char **source, int *lineDefined, int *currentLine) {
  if (!S || !what)
    return 0;

  spt::FiberObject *fiber = S->fiber;

  int idx = fiber->frameCount - 1 - level;
  if (idx < 0 || idx >= fiber->frameCount)
    return 0;

  const spt::CallFrame &frame = fiber->frames[idx];
  if (!frame.closure)
    return 0;

  pushValue(S, spt::Value::object(frame.closure));
  int result = spt_getinfo(S, -1, what, name, source, lineDefined, currentLine);
  S->fiber->pop();

  return result;
}

SPT_API int spt_gc(spt_State *S, int what, int data) {
  if (!S)
    return 0;

  spt::GC &gc = S->vm->gc();

  switch (what) {
  case SPT_GCSTOP:
    gc.setEnabled(false);
    return 0;

  case SPT_GCRESTART:
    gc.setEnabled(true);
    return 0;

  case SPT_GCCOLLECT:
    gc.collect();
    return 0;

  case SPT_GCCOUNT:
    return static_cast<int>(gc.bytesAllocated() / 1024);

  case SPT_GCCOUNTB:
    return static_cast<int>(gc.bytesAllocated());

  case SPT_GCSTEP:
    gc.collectIfNeeded();
    return 0;

  case SPT_GCISRUNNING:
    return 1;

  case SPT_GCOBJCOUNT:
    return static_cast<int>(gc.objectCount());

  default:
    return 0;
  }
}

SPT_API void spt_register(spt_State *S, const char *libname, const spt_Reg *funcs) {
  if (!S || !funcs)
    return;

  if (libname) {

    spt_newmap(S, 16);

    for (const spt_Reg *f = funcs; f->name != nullptr; ++f) {
      spt::Closure *closure = createCClosure(S, f->func, 0);
      closure->arity = f->arity;
      closure->name = S->vm->allocateString(f->name);

      spt_pushstring(S, f->name);
      pushValue(S, spt::Value::object(closure));
      spt_setmap(S, -3);
    }

    spt_setglobal(S, libname);
  } else {

    for (const spt_Reg *f = funcs; f->name != nullptr; ++f) {
      spt::Closure *closure = createCClosure(S, f->func, 0);
      closure->arity = f->arity;
      closure->name = S->vm->allocateString(f->name);

      pushValue(S, spt::Value::object(closure));
      spt_setglobal(S, f->name);
    }
  }
}

SPT_API void spt_registermethods(spt_State *S, int class_idx, const spt_MethodReg *methods) {
  if (!S || !methods)
    return;

  spt::Value *classPtr = getValuePtr(S, class_idx);
  if (!classPtr || !classPtr->isClass())
    return;

  spt::ClassObject *klass = static_cast<spt::ClassObject *>(classPtr->asGC());

  for (const spt_MethodReg *m = methods; m->name != nullptr; ++m) {
    spt::Closure *closure = createCClosure(S, m->func, 0);
    closure->arity = m->arity;
    closure->name = S->vm->allocateString(m->name);

    spt::StringObject *nameStr = S->vm->allocateString(m->name);

    if (m->isStatic) {
      klass->statics[nameStr] = spt::Value::object(closure);
    } else {
      klass->setMethod(nameStr, spt::Value::object(closure));
    }
  }
}

SPT_API void spt_openlibs(spt_State *S) {
  if (!S)
    return;
}

SPT_API size_t spt_len(spt_State *S, int idx) {
  if (!S)
    return 0;

  spt::Value v = getValue(S, idx);

  if (v.isString()) {
    return v.asString()->length;
  } else if (v.isList()) {
    return static_cast<spt::ListObject *>(v.asGC())->elements.size();
  } else if (v.isMap()) {
    return static_cast<spt::MapObject *>(v.asGC())->entries.size();
  }

  return 0;
}

SPT_API void spt_concat(spt_State *S, int n) {
  if (!S || n <= 0)
    return;

  std::string result;

  for (int i = n; i > 0; --i) {
    spt::Value v = getValue(S, -i);
    result += v.toString();
  }

  spt_settop(S, -(n + 1));

  spt::StringObject *str = S->vm->allocateString(result);
  pushValue(S, spt::Value::object(str));
}

SPT_API void spt_tostr(spt_State *S, int idx) {
  if (!S)
    return;

  spt::Value v = getValue(S, idx);
  std::string str = v.toString();

  spt::StringObject *strObj = S->vm->allocateString(str);
  pushValue(S, spt::Value::object(strObj));
}

SPT_API const char *spt_internstring(spt_State *S, const char *str, size_t len) {
  if (!S || !str)
    return nullptr;

  spt::StringObject *strObj = S->vm->allocateString(std::string_view(str, len));
  return strObj->c_str();
}

SPT_API void spt_argcheck(spt_State *S, int cond, int arg, const char *msg) {
  if (!cond) {
    spt_argerror(S, arg, msg);
  }
}

SPT_API int spt_argerror(spt_State *S, int arg, const char *msg) {
  if (S) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "bad argument #%d (%s)", arg, msg ? msg : "invalid value");
    spt_error(S, "%s", buf);
  }
  return 0;
}

SPT_API int spt_typeerror(spt_State *S, int arg, const char *tname) {
  if (S) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "bad argument #%d (%s expected)", arg, tname ? tname : "?");
    spt_error(S, "%s", buf);
  }
  return 0;
}

SPT_API spt_Int spt_checkint(spt_State *S, int arg) {
  int isnum;
  spt_Int val = spt_tointx(S, arg, &isnum);
  if (!isnum) {
    spt_typeerror(S, arg, "int");
  }
  return val;
}

SPT_API spt_Float spt_checkfloat(spt_State *S, int arg) {
  int isnum;
  spt_Float val = spt_tofloatx(S, arg, &isnum);
  if (!isnum) {
    spt_typeerror(S, arg, "float");
  }
  return val;
}

SPT_API const char *spt_checkstring(spt_State *S, int arg, size_t *len) {
  const char *str = spt_tostring(S, arg, len);
  if (!str) {
    spt_typeerror(S, arg, "string");
    return "";
  }
  return str;
}

SPT_API void spt_checktype(spt_State *S, int arg, int tp) {
  if (spt_type(S, arg) != tp) {
    spt_typeerror(S, arg, spt_typename(S, tp));
  }
}

SPT_API void spt_checkany(spt_State *S, int arg) {
  if (spt_type(S, arg) == SPT_TNONE) {
    spt_argerror(S, arg, "value expected");
  }
}

SPT_API spt_Int spt_optint(spt_State *S, int arg, spt_Int def) {
  if (spt_isnoneornil(S, arg))
    return def;
  return spt_checkint(S, arg);
}

SPT_API spt_Float spt_optfloat(spt_State *S, int arg, spt_Float def) {
  if (spt_isnoneornil(S, arg))
    return def;
  return spt_checkfloat(S, arg);
}

SPT_API const char *spt_optstring(spt_State *S, int arg, const char *def) {
  if (spt_isnoneornil(S, arg))
    return def;
  return spt_checkstring(S, arg, nullptr);
}

SPT_API void *spt_checklightuserdata(spt_State *S, int arg) {
  if (!spt_islightuserdata(S, arg)) {
    spt_typeerror(S, arg, "lightuserdata");
    return nullptr;
  }
  return spt_tolightuserdata(S, arg);
}

SPT_API void *spt_optlightuserdata(spt_State *S, int arg, void *def) {
  if (spt_isnoneornil(S, arg))
    return def;
  return spt_checklightuserdata(S, arg);
}

SPT_API int spt_listiter(spt_State *S, int idx) {
  if (!S)
    return -1;

  spt::Value v = getValue(S, idx);
  if (!v.isList())
    return -1;

  return 0;
}

SPT_API int spt_listnext(spt_State *S, int idx, int *iter) {
  if (!S || !iter)
    return 0;

  spt::Value v = getValue(S, idx);
  if (!v.isList())
    return 0;

  spt::ListObject *list = static_cast<spt::ListObject *>(v.asGC());

  if (*iter >= static_cast<int>(list->elements.size())) {
    return 0;
  }

  pushValue(S, list->elements[*iter]);
  (*iter)++;

  return 1;
}

SPT_API int spt_rawget(spt_State *S, int idx) {
  if (!S)
    return SPT_TNIL;

  spt::Value *t = getValuePtr(S, idx);
  spt::Value key = S->fiber->pop();
  spt::Value result = spt::Value::nil();

  if (t) {
    if (t->isInstance()) {

      spt::Instance *inst = static_cast<spt::Instance *>(t->asGC());
      if (key.isString()) {
        result = inst->getField(key.asString());
      }
    } else if (t->isNativeInstance()) {
      spt::NativeInstance *inst = static_cast<spt::NativeInstance *>(t->asGC());
      if (key.isString()) {
        result = inst->getField(key.asString());
      }
    } else if (t->isMap()) {
      spt::MapObject *map = static_cast<spt::MapObject *>(t->asGC());
      result = map->get(key);
    } else if (t->isList()) {
      spt::ListObject *list = static_cast<spt::ListObject *>(t->asGC());
      if (key.isInt()) {
        int64_t index = key.asInt();
        if (index >= 0 && index < static_cast<int64_t>(list->elements.size())) {
          result = list->elements[index];
        }
      }
    }
  }

  pushValue(S, result);
  return valueTypeToSptType(result.type);
}

SPT_API void spt_rawset(spt_State *S, int idx) {
  if (!S)
    return;

  spt::Value *t = getValuePtr(S, idx);
  spt::Value value = S->fiber->pop();
  spt::Value key = S->fiber->pop();

  if (!t)
    return;

  if (t->isInstance()) {
    spt::Instance *inst = static_cast<spt::Instance *>(t->asGC());
    if (key.isString()) {
      inst->setField(key.asString(), value);
    }
  } else if (t->isNativeInstance()) {
    spt::NativeInstance *inst = static_cast<spt::NativeInstance *>(t->asGC());
    if (key.isString()) {
      inst->setField(key.asString(), value);
    }
  } else if (t->isMap()) {
    spt::MapObject *map = static_cast<spt::MapObject *>(t->asGC());
    map->set(key, value);
  } else if (t->isList()) {
    spt::ListObject *list = static_cast<spt::ListObject *>(t->asGC());
    if (key.isInt()) {
      int64_t index = key.asInt();
      if (index >= 0 && index < static_cast<int64_t>(list->elements.size())) {
        list->elements[index] = value;
      }
    }
  }
}

SPT_API int spt_yieldk(spt_State *S, int nresults, spt_KContext ctx, spt_KFunction k) {
  if (!S || !S->fiber)
    return 0;

  spt::FiberObject *fiber = S->fiber;
  if (fiber->frameCount == 0)
    return 0;

  spt::CallFrame &frame = fiber->frames[fiber->frameCount - 1];

  if (frame.closure && frame.closure->isNative()) {
    frame.continuation = reinterpret_cast<spt::KFunction>(k);
    frame.ctx = ctx;
    frame.status = SPT_YIELD;
  }

  spt::Value value = spt::Value::nil();
  if (nresults > 0) {
    value = S->fiber->pop();
  }
  S->vm->fiberYield(value);

  return 0;
}

SPT_API const char *spt_version(void) { return SPT_VERSION_STRING; }

SPT_API int spt_versionnum(void) { return SPT_VERSION_NUM; }
}