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
    inline int propagate_exception(lua_State* L) {
        std::string err_msg;
        try {
            throw;
        } catch (const error& e) {
            err_msg = e.what();
        } catch (const std::exception& e) {
            err_msg = e.what();
        } catch (...) {
            err_msg = "Unknown C++ exception";
        }
        lua_pushstring(L, err_msg.c_str());
        return lua_error(L);
    }
    
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