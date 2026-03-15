// sptxx.hpp - Main header file for SPT Lua 5.5 C++ bindings
// Similar to sol2 but adapted for SPT's Slot 0 Receiver convention and List/Map separation

#pragma once

#include "sptxx/state.hpp"
#include "sptxx/list.hpp"
#include "sptxx/map.hpp"
#include "sptxx/function.hpp"
#include "sptxx/usertype.hpp"
#include "sptxx/stack.hpp"
#include "sptxx/error.hpp"

// Convenience namespace alias
namespace spt = sptxx;