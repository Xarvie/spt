#pragma once
#include "Value.h"
#include <string>

namespace spt {
class VM;

class StdlibDispatcher {
public:
  static bool getProperty(VM *vm, Value object, const std::string &fieldName, Value &outValue);
  static bool invokeMethod(VM *vm, Value receiver, const std::string &methodName, int argc,
                           Value *argv, Value &outResult);
};

} // namespace spt