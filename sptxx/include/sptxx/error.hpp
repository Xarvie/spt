// error.hpp - Error handling for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include <exception>
#include <string>

namespace sptxx {

class error : public std::exception {
private:
    std::string msg;
    
public:
    explicit error(const std::string& message) : msg(message) {}
    explicit error(std::string&& message) : msg(std::move(message)) {}
    
    const char* what() const noexcept override {
        return msg.c_str();
    }
};

class runtime_error : public error {
public:
    using error::error;
};

class type_error : public error {
public:
    using error::error;
};

namespace detail {
    // Helper to propagate C++ exceptions as Lua errors
    inline int propagate_exception(lua_State* L) {
        try {
            throw; // Re-throw the current exception
        } catch (const error& e) {
            lua_pushstring(L, e.what());
            return lua_error(L);
        } catch (const std::exception& e) {
            lua_pushstring(L, e.what());
            return lua_error(L);
        } catch (...) {
            lua_pushstring(L, "Unknown C++ exception");
            return lua_error(L);
        }
    }
    
    // Helper to handle Lua errors as C++ exceptions
    inline void handle_lua_error(lua_State* L, int status) {
        if (status != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            if (msg) {
                std::string error_msg(msg);
                lua_pop(L, 1);
                throw runtime_error(error_msg);
            } else {
                throw runtime_error("Unknown Lua error");
            }
        }
    }
}

} // namespace sptxx