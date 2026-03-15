// usertype.hpp - Class binding system for SPT Lua 5.5 C++ bindings
// Fixed: member pointers stored in userdata via memcpy (cannot reinterpret_cast to void*)
// Fixed: proper __index/__newindex dispatch for member variables and methods

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include "stack.hpp"
#include "function.hpp"
#include "error.hpp"
#include <string>
#include <type_traits>
#include <memory>
#include <cstring>

namespace sptxx {

template<typename T>
class usertype {
private:
    lua_State* L;
    std::string type_name;
    
public:
    usertype(usertype&& other) noexcept : L(other.L), type_name(std::move(other.type_name)) {}
    
    usertype& operator=(usertype&& other) noexcept {
        if (this != &other) {
            L = other.L;
            type_name = std::move(other.type_name);
        }
        return *this;
    }
    
    usertype(const usertype&) = delete;
    usertype& operator=(const usertype&) = delete;
    
    usertype(lua_State* state, const char* name) : L(state), type_name(name) {
        luaL_newmetatable(L, name);
        
        // Type name
        lua_pushstring(L, name);
        lua_setfield(L, -2, "__name");
        
        // GC
        lua_pushcfunction(L, &gc_method);
        lua_setfield(L, -2, "__gc");
        
        // Custom __index handler (checks __getters, then __methods, then metatable)
        lua_pushcfunction(L, &index_handler);
        lua_setfield(L, -2, "__index");
        
        // Custom __newindex handler (checks __setters)
        lua_pushcfunction(L, &newindex_handler);
        lua_setfield(L, -2, "__newindex");
        
        // Subtables for getters, setters, and methods
        lua_newtable(L);
        lua_setfield(L, -2, "__getters");
        
        lua_newtable(L);
        lua_setfield(L, -2, "__setters");
        
        lua_newtable(L);
        lua_setfield(L, -2, "__methods");
        
        lua_pop(L, 1); // pop metatable
    }
    
    ~usertype() = default;
    
    // --------------- Metamethods ---------------
    
    static int gc_method(lua_State* L) {
        T** ptr = static_cast<T**>(lua_touserdata(L, 1));
        if (ptr && *ptr) {
            delete *ptr;
            *ptr = nullptr;
        }
        return 0;
    }
    
    // __index: getters -> methods -> raw metatable fields
    static int index_handler(lua_State* L) {
        // stack: [userdata, key]
        const char* key = lua_tostring(L, 2);
        if (!key) return 0;
        
        lua_getmetatable(L, 1); // stack: [ud, key, mt]
        
        // 1) Check __getters
        lua_getfield(L, -1, "__getters");   // [ud, key, mt, getters]
        lua_getfield(L, -1, key);           // [ud, key, mt, getters, getter?]
        if (!lua_isnil(L, -1)) {
            // getter is a closure(self) -> value
            lua_pushvalue(L, 1);            // push self
            lua_call(L, 1, 1);
            return 1;
        }
        lua_pop(L, 2); // pop nil + __getters
        
        // 2) Check __methods
        lua_getfield(L, -1, "__methods");   // [ud, key, mt, methods]
        lua_getfield(L, -1, key);           // [ud, key, mt, methods, method?]
        if (!lua_isnil(L, -1)) {
            return 1; // return the method closure
        }
        lua_pop(L, 2); // pop nil + __methods
        
        // 3) Check metatable directly (e.g. "new", "__name", etc.)
        lua_getfield(L, -1, key);
        if (!lua_isnil(L, -1)) {
            return 1;
        }
        
        return 0;
    }
    
    // __newindex: setters -> error
    static int newindex_handler(lua_State* L) {
        // stack: [userdata, key, value]
        const char* key = lua_tostring(L, 2);
        if (!key) return luaL_error(L, "field name must be a string");
        
        lua_getmetatable(L, 1);
        lua_getfield(L, -1, "__setters");
        lua_getfield(L, -1, key);
        if (!lua_isnil(L, -1)) {
            // setter is a closure(self, value) -> void
            lua_pushvalue(L, 1); // self
            lua_pushvalue(L, 3); // value
            lua_call(L, 2, 0);
            return 0;
        }
        
        return luaL_error(L, "cannot set field '%s'", key);
    }
    
    // --------------- Member variable registration ---------------
    // Stores member pointer in a full userdata upvalue via memcpy.
    
    template<typename U>
    void set(const char* name, U T::* member) {
        luaL_getmetatable(L, type_name.c_str());
        
        // Register getter closure
        lua_getfield(L, -1, "__getters");
        {
            void* storage = lua_newuserdatauv(L, sizeof(U T::*), 0);
            std::memcpy(storage, &member, sizeof(U T::*));
            lua_pushcclosure(L, &member_getter<U>, 1);
            lua_setfield(L, -2, name);
        }
        lua_pop(L, 1); // pop __getters
        
        // Register setter closure
        lua_getfield(L, -1, "__setters");
        {
            void* storage = lua_newuserdatauv(L, sizeof(U T::*), 0);
            std::memcpy(storage, &member, sizeof(U T::*));
            lua_pushcclosure(L, &member_setter<U>, 1);
            lua_setfield(L, -2, name);
        }
        lua_pop(L, 1); // pop __setters
        
        lua_pop(L, 1); // pop metatable
    }
    
    // --------------- Method registration (specific overloads) ---------------
    
    // R (T::*)()
    template<typename R>
    void set(const char* name, R(T::*func)()) {
        using FuncType = R(T::*)();
        register_method_closure<FuncType, R>(name, func);
    }
    
    // R (T::*)() const
    template<typename R>
    void set(const char* name, R(T::*func)() const) {
        using FuncType = R(T::*)() const;
        register_method_closure<FuncType, R>(name, func);
    }
    
    // R (T::*)(A1)
    template<typename R, typename A1>
    void set(const char* name, R(T::*func)(A1)) {
        using FuncType = R(T::*)(A1);
        register_method1_closure<FuncType, R, A1>(name, func);
    }
    
    // R (T::*)(A1) const
    template<typename R, typename A1>
    void set(const char* name, R(T::*func)(A1) const) {
        using FuncType = R(T::*)(A1) const;
        register_method1_closure<FuncType, R, A1>(name, func);
    }
    
    // R (T::*)(A1, A2)
    template<typename R, typename A1, typename A2>
    void set(const char* name, R(T::*func)(A1, A2)) {
        using FuncType = R(T::*)(A1, A2);
        register_method2_closure<FuncType, R, A1, A2>(name, func);
    }
    
    // R (T::*)(A1, A2) const
    template<typename R, typename A1, typename A2>
    void set(const char* name, R(T::*func)(A1, A2) const) {
        using FuncType = R(T::*)(A1, A2) const;
        register_method2_closure<FuncType, R, A1, A2>(name, func);
    }
    
    // --------------- Constructor ---------------
    
    template<typename... Args>
    void constructor() {
        auto ctor_wrapper = [](lua_State* L) -> int {
            const char* name = lua_tostring(L, lua_upvalueindex(1));
            try {
                void* obj = lua_newuserdatauv(L, sizeof(T*), 0);
                luaL_getmetatable(L, name);
                lua_setmetatable(L, -2);
                T** ptr = static_cast<T**>(obj);
                *ptr = new T();
                return 1;
            } catch (...) {
                return detail::propagate_exception(L);
            }
        };
        
        luaL_getmetatable(L, type_name.c_str());
        lua_pushstring(L, type_name.c_str());
        lua_pushcclosure(L, ctor_wrapper, 1);
        lua_setfield(L, -2, "new");
        lua_pop(L, 1);
    }
    
private:
    // --------------- Member variable getter / setter closures ---------------
    // upvalue 1: userdata containing the member pointer (memcpy'd)
    
    template<typename U>
    static int member_getter(lua_State* L) {
        T** ptr = static_cast<T**>(lua_touserdata(L, 1));
        if (!ptr || !*ptr) return luaL_error(L, "null object in getter");
        
        U T::* member;
        std::memcpy(&member, lua_touserdata(L, lua_upvalueindex(1)), sizeof(member));
        
        stack::push(L, (*ptr)->*member);
        return 1;
    }
    
    template<typename U>
    static int member_setter(lua_State* L) {
        T** ptr = static_cast<T**>(lua_touserdata(L, 1));
        if (!ptr || !*ptr) return luaL_error(L, "null object in setter");
        
        U T::* member;
        std::memcpy(&member, lua_touserdata(L, lua_upvalueindex(1)), sizeof(member));
        
        (*ptr)->*member = stack::get<std::decay_t<U>>(L, 2);
        return 0;
    }
    
    // --------------- Method closure helpers ---------------
    // All store the member-function pointer in a full userdata upvalue.
    
    // Helper: push a method closure into __methods[name]
    template<typename FuncType>
    void store_method_upvalue(const char* name, FuncType func, lua_CFunction wrapper) {
        luaL_getmetatable(L, type_name.c_str());
        lua_getfield(L, -1, "__methods");
        
        void* storage = lua_newuserdatauv(L, sizeof(FuncType), 0);
        std::memcpy(storage, &func, sizeof(FuncType));
        lua_pushcclosure(L, wrapper, 1);
        lua_setfield(L, -2, name);
        
        lua_pop(L, 2); // pop __methods + metatable
    }
    
    // 0-arg method returning R (or void)
    template<typename FuncType, typename R>
    void register_method_closure(const char* name, FuncType func) {
        store_method_upvalue(name, func, [](lua_State* L) -> int {
            T** ptr = static_cast<T**>(lua_touserdata(L, 1));
            if (!ptr || !*ptr) return luaL_error(L, "null object in method call");
            
            FuncType f;
            std::memcpy(&f, lua_touserdata(L, lua_upvalueindex(1)), sizeof(f));
            
            if constexpr (std::is_void_v<R>) {
                ((*ptr)->*f)();
                return 0;
            } else {
                R result = ((*ptr)->*f)();
                stack::push(L, result);
                return 1;
            }
        });
    }
    
    // 1-arg method
    template<typename FuncType, typename R, typename A1>
    void register_method1_closure(const char* name, FuncType func) {
        store_method_upvalue(name, func, [](lua_State* L) -> int {
            T** ptr = static_cast<T**>(lua_touserdata(L, 1));
            if (!ptr || !*ptr) return luaL_error(L, "null object in method call");
            
            FuncType f;
            std::memcpy(&f, lua_touserdata(L, lua_upvalueindex(1)), sizeof(f));
            
            auto arg1 = stack::get<std::decay_t<A1>>(L, 2);
            
            if constexpr (std::is_void_v<R>) {
                ((*ptr)->*f)(arg1);
                return 0;
            } else {
                R result = ((*ptr)->*f)(arg1);
                stack::push(L, result);
                return 1;
            }
        });
    }
    
    // 2-arg method
    template<typename FuncType, typename R, typename A1, typename A2>
    void register_method2_closure(const char* name, FuncType func) {
        store_method_upvalue(name, func, [](lua_State* L) -> int {
            T** ptr = static_cast<T**>(lua_touserdata(L, 1));
            if (!ptr || !*ptr) return luaL_error(L, "null object in method call");
            
            FuncType f;
            std::memcpy(&f, lua_touserdata(L, lua_upvalueindex(1)), sizeof(f));
            
            auto arg1 = stack::get<std::decay_t<A1>>(L, 2);
            auto arg2 = stack::get<std::decay_t<A2>>(L, 3);
            
            if constexpr (std::is_void_v<R>) {
                ((*ptr)->*f)(arg1, arg2);
                return 0;
            } else {
                R result = ((*ptr)->*f)(arg1, arg2);
                stack::push(L, result);
                return 1;
            }
        });
    }
};

} // namespace sptxx