#pragma once
#include "Value.h"
#include <string>

namespace spt {
class VM;

class StdlibDispatcher {
public:
  // 从object中获取property或method
  static bool getProperty(VM *vm, Value object, const std::string &fieldName, Value &outValue);

  // 为object设置property或method
  static bool setProperty(VM *vm, Value object, const std::string &fieldName, const Value &value);

  // 调用object的方法
  static bool invokeMethod(VM *vm, Value receiver, const std::string &methodName, int argc,
                           Value *argv, Value &outResult);
};

} // namespace spt
