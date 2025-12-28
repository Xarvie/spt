#pragma once

/**
 * @file SptStdlibs.h
 * @brief sptScript Standard Library - Native Methods for List, Map, String
 *
 * Implements the standard library by binding NativeFunctions to objects.
 * Architecture: Unified NativeFunction (Lua-style C Closures)
 *
 * Supports two calling patterns:
 *   1. OP_INVOKE: Direct method invocation with receiver passed at runtime
 *   2. OP_GETFIELD + OP_CALL: Bound method with pre-captured receiver
 *
 * @version 2.0 (OP_INVOKE Support)
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
   *
   * For properties (like length, size): Returns the value directly.
   * For methods: Returns a bound NativeFunction for GETFIELD+CALL pattern,
   *              or can be used with OP_INVOKE for direct invocation.
   *
   * @param vm The virtual machine instance
   * @param object The receiver object (List, Map, or String)
   * @param fieldName The property or method name to access
   * @param outValue [out] The result value (property value or bound NativeFunction)
   * @return true if the field was handled, false if not found
   */
  static bool getProperty(VM *vm, Value object, const std::string &fieldName, Value &outValue);

  /**
   * @brief Direct method invocation for OP_INVOKE optimization
   *
   * This bypasses NativeFunction creation for better performance.
   * Used when OP_INVOKE knows the receiver type at dispatch time.
   *
   * @param vm The virtual machine instance
   * @param receiver The receiver object
   * @param methodName The method name to invoke
   * @param argc Argument count (not including receiver)
   * @param argv Argument values
   * @param outResult [out] The method return value
   * @return true if method was found and invoked, false if not found
   */
  static bool invokeMethod(VM *vm, Value receiver, const std::string &methodName, int argc,
                           Value *argv, Value &outResult);
};

} // namespace spt