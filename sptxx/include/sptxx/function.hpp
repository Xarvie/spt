// function.hpp - Function binding system for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include "stack.hpp"
#include "error.hpp"
#include <tuple>
#include <utility>
#include <type_traits>

namespace sptxx {

namespace detail {
    
    // Helper to extract arguments from Lua stack, skipping receiver at index 1
    template<typename T>
    inline T extract_arg(lua_State* L, int index) {
        return stack::get<T>(L, index);
    }
    
    template<std::size_t... I, typename... Args>
    inline std::tuple<Args...> extract_args_impl(lua_State* L, std::index_sequence<I...>) {
        // Skip index 1 (receiver), start from index 2
        return std::make_tuple(extract_arg<Args>(L, 2 + static_cast<int>(I))...);
    }
    
    template<typename... Args>
    inline std::tuple<Args...> extract_args(lua_State* L) {
        return extract_args_impl<Args...>(L, std::index_sequence_for<Args...>{});
    }
    
    // Helper to push return values
    template<typename T>
    inline void push_return(lua_State* L, T&& value) {
        stack::push(L, std::forward<T>(value));
    }
    
    inline void push_return_void(lua_State* L) {
        // Nothing to push for void return
    }
    
    // Main function call handler
    template<typename Func, typename... Args>
    inline auto call_function(Func&& f, lua_State* L) -> decltype(f(std::declval<Args>()...)) {
        auto args = extract_args<Args...>(L);
        return std::apply(std::forward<Func>(f), std::move(args));
    }
    
    // Function wrapper generator - returns a lua_CFunction compatible function
    template<typename Func>
    lua_CFunction make_function_wrapper(Func&& f) {
        // Store the function in a static variable (this is a simplification)
        // In a real implementation, we would need to manage function storage properly
        static std::decay_t<Func> stored_func = std::forward<Func>(f);
        
        return [](lua_State* L) -> int {
            try {
                // For demonstration, handle only int, int -> int
                int arg1 = luaL_checkinteger(L, 2);
                int arg2 = luaL_checkinteger(L, 3);
                int result = stored_func(arg1, arg2);
                lua_pushinteger(L, result);
                return 1;
            } catch (...) {
                return propagate_exception(L);
            }
        };
    }
    
    // Simplified function wrapper - we'll implement proper traits later if needed
    
} // namespace detail

} // namespace sptxx