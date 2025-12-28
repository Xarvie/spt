#pragma once

/**
 * @file Stdlib.h
 * @brief sptScript Standard Library - Native Methods for List, Map, String
 * * Implements the standard library by binding NativeFunctions to objects.
 * Architecture: Unified NativeFunction (Lua-style C Closures)
 * * @version 1.1 (Unified Architecture)
 */

#include "Value.h"
#include <string>

namespace spt {

// Forward declarations
class VM;

// ============================================================================
// StdlibDispatcher - Central dispatch for built-in type properties/methods
// ============================================================================

class StdlibDispatcher {
public:
  /**
   * @brief Attempt to get a property or method from a built-in type
   * * @param vm The virtual machine instance
   * @param object The receiver object (List, Map, or String)
   * @param fieldName The property or method name to access
   * @param outValue [out] The result value (property value or bound NativeFunction)
   * @return true if the field was handled, false if not found
   */
  static bool getProperty(VM *vm, Value object, const std::string &fieldName, Value &outValue);
};

} // namespace spt