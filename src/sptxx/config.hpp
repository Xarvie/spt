#ifndef SPTXX_CONFIG_HPP
#define SPTXX_CONFIG_HPP

// ============================================================================
// Version Information
// ============================================================================

#define SPTXX_VERSION_MAJOR 1
#define SPTXX_VERSION_MINOR 0
#define SPTXX_VERSION_PATCH 0
#define SPTXX_VERSION_STRING "1.0.0"
#define SPTXX_VERSION_NUM                                                                          \
  (SPTXX_VERSION_MAJOR * 10000 + SPTXX_VERSION_MINOR * 100 + SPTXX_VERSION_PATCH)

// ============================================================================
// Compiler Detection
// ============================================================================

#if defined(_MSC_VER)
#define SPTXX_COMPILER_MSVC 1
#define SPTXX_COMPILER_VERSION _MSC_VER
#elif defined(__clang__)
#define SPTXX_COMPILER_CLANG 1
#define SPTXX_COMPILER_VERSION                                                                     \
  (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#elif defined(__GNUC__)
#define SPTXX_COMPILER_GCC 1
#define SPTXX_COMPILER_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#else
#define SPTXX_COMPILER_UNKNOWN 1
#define SPTXX_COMPILER_VERSION 0
#endif

// ============================================================================
// Platform Detection
// ============================================================================

#if defined(_WIN32) || defined(_WIN64)
#define SPTXX_PLATFORM_WINDOWS 1
#elif defined(__linux__)
#define SPTXX_PLATFORM_LINUX 1
#elif defined(__APPLE__)
#define SPTXX_PLATFORM_MACOS 1
#else
#define SPTXX_PLATFORM_UNKNOWN 1
#endif

// ============================================================================
// C++ Standard Detection
// ============================================================================

#if defined(_MSVC_LANG)
#define SPTXX_CPLUSPLUS _MSVC_LANG
#else
#define SPTXX_CPLUSPLUS __cplusplus
#endif

#if SPTXX_CPLUSPLUS >= 202002L
#define SPTXX_CPP20_OR_LATER 1
#endif

#if SPTXX_CPLUSPLUS >= 201703L
#define SPTXX_CPP17_OR_LATER 1
#endif

#if SPTXX_CPLUSPLUS >= 201402L
#define SPTXX_CPP14_OR_LATER 1
#endif

#if SPTXX_CPLUSPLUS < 201703L
#error "SPT++ requires C++17 or later"
#endif

// ============================================================================
// Feature Macros
// ============================================================================

// Inline Hints
#if defined(SPTXX_COMPILER_MSVC)
#define SPTXX_FORCEINLINE __forceinline
#define SPTXX_NOINLINE __declspec(noinline)
#elif defined(SPTXX_COMPILER_GCC) || defined(SPTXX_COMPILER_CLANG)
#define SPTXX_FORCEINLINE __attribute__((always_inline)) inline
#define SPTXX_NOINLINE __attribute__((noinline))
#else
#define SPTXX_FORCEINLINE inline
#define SPTXX_NOINLINE
#endif

// Likely/Unlikely Branch Hints
#if defined(SPTXX_CPP20_OR_LATER)
#define SPTXX_LIKELY [[likely]]
#define SPTXX_UNLIKELY [[unlikely]]
#elif defined(SPTXX_COMPILER_GCC) || defined(SPTXX_COMPILER_CLANG)
#define SPTXX_LIKELY
#define SPTXX_UNLIKELY
#define sptxx_likely(x) __builtin_expect(!!(x), 1)
#define sptxx_unlikely(x) __builtin_expect(!!(x), 0)
#else
#define SPTXX_LIKELY
#define SPTXX_UNLIKELY
#define sptxx_likely(x) (x)
#define sptxx_unlikely(x) (x)
#endif

#ifndef sptxx_likely
#define sptxx_likely(x) (x)
#define sptxx_unlikely(x) (x)
#endif

// Nodiscard
#define SPTXX_NODISCARD [[nodiscard]]

// Maybe Unused
#define SPTXX_MAYBE_UNUSED [[maybe_unused]]

// Fallthrough
#define SPTXX_FALLTHROUGH [[fallthrough]]

// ============================================================================
// Export/Import Macros
// ============================================================================

#if defined(SPTXX_PLATFORM_WINDOWS)
#if defined(SPTXX_BUILD_SHARED)
#if defined(SPTXX_EXPORTS)
#define SPTXX_API __declspec(dllexport)
#else
#define SPTXX_API __declspec(dllimport)
#endif
#else
#define SPTXX_API
#endif
#else
#if defined(SPTXX_BUILD_SHARED)
#define SPTXX_API __attribute__((visibility("default")))
#else
#define SPTXX_API
#endif
#endif

// ============================================================================
// Configuration Options
// ============================================================================

// Enable/Disable Exception Support
#ifndef SPTXX_NO_EXCEPTIONS
#define SPTXX_EXCEPTIONS_ENABLED 1
#endif

// Enable/Disable RTTI
#ifndef SPTXX_NO_RTTI
#define SPTXX_RTTI_ENABLED 1
#endif

// Enable/Disable Thread Safety
#ifndef SPTXX_NO_THREAD_SAFETY
#define SPTXX_THREAD_SAFETY_ENABLED 1
#endif

// Enable/Disable Debug Checks
#if defined(NDEBUG) && !defined(SPTXX_DEBUG)
#define SPTXX_RELEASE_MODE 1
#else
#define SPTXX_DEBUG_MODE 1
#endif

// Stack Check Mode
#ifndef SPTXX_STACK_CHECK_DISABLE
#define SPTXX_STACK_CHECK_ENABLED 1
#endif

// Default Stack Size for Protected Calls
#ifndef SPTXX_DEFAULT_STACK_SIZE
#define SPTXX_DEFAULT_STACK_SIZE 20
#endif

// ============================================================================
// Debug Assertions
// ============================================================================

#if defined(SPTXX_DEBUG_MODE)
#include <cassert>
#define SPTXX_ASSERT(cond, msg) assert((cond) && (msg))
#define SPTXX_DEBUG_ASSERT(cond, msg) assert((cond) && (msg))
#else
#define SPTXX_ASSERT(cond, msg) ((void)0)
#define SPTXX_DEBUG_ASSERT(cond, msg) ((void)0)
#endif

// ============================================================================
// Exception or Error Code Mode
// ============================================================================

#if defined(SPTXX_EXCEPTIONS_ENABLED)
#define SPTXX_TRY try
#define SPTXX_CATCH(x) catch (x)
#define SPTXX_THROW(x) throw x
#define SPTXX_RETHROW throw
#else
#define SPTXX_TRY if (true)
#define SPTXX_CATCH(x) if (false)
#define SPTXX_THROW(x) std::abort()
#define SPTXX_RETHROW std::abort()
#endif

// ============================================================================
// Namespace Configuration
// ============================================================================

#ifndef SPTXX_NAMESPACE
#define SPTXX_NAMESPACE spt
#endif

#define SPTXX_NAMESPACE_BEGIN namespace SPTXX_NAMESPACE {
#define SPTXX_NAMESPACE_END }

// ============================================================================
// Include Standard Headers
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#endif // SPTXX_CONFIG_HPP
