#include "StringPool.h"
#include "GC.h"
#include <algorithm>
#include <cstring>

namespace spt {

StringObject *StringPool::intern(std::string_view sv) {

  auto it = strings_.find(sv);
  if (it != strings_.end()) {
    return *it;
  }

  uint32_t hash = fnv1a_hash(sv);

  StringObject *str = gc_->allocateString(sv, hash);

  strings_.insert(str);

  return str;
}

StringObject *StringPool::find(std::string_view sv) const {
  auto it = strings_.find(sv);
  return (it != strings_.end()) ? *it : nullptr;
}

void StringPool::remove(StringObject *str) {
  if (str) {
    strings_.erase(str);
  }
}

void StringPool::removeWhiteStrings() {

  std::erase_if(strings_, [](StringObject *str) { return str && !str->marked; });
}

} // namespace spt
