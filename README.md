# SptScript

[![Compile Check](https://github.com/Xarvie/spt/actions/workflows/compile.yml/badge.svg?event=push)](https://github.com/Xarvie/spt/actions/workflows/compile.yml)

## todo
gc问题
for循环
冒号语法
c嵌入api
call stack行号（source map）

list-map-string:
================================================================================
sptScript Standard Library API Specification v1.0
Target: Commercial-Grade Stability & System Predictability
================================================================================

[Design Philosophy]

1. No Implicit Allocations: Slicing/Keys always return new deep copies.
2. Byte Semantics: Strings are UTF-8 bytes; indices are byte offsets.
3. Explicit Complexity: Users are responsible for O(N) operations.
4. Three-State Maps: Key missing vs Key is nil vs Key is value.

================================================================================

1. List (Array-backed)
   ================================================================================
   [Properties]
   .length -> int
   - Read-only property.
   - Returns the number of elements.
   - O(1).

[Methods]
.push(val)          -> void

- Appends 'val' to the end.
- Returns nothing (void) to encourage builder patterns if needed later.
- Amortized O(1).

.pop()              -> val | nil

- Removes and returns the last element.
- Returns 'nil' if the list is empty.
- O(1).

.insert(idx, val)   -> void

- Inserts 'val' at 'idx'. Subsequent elements are shifted right.
- O(N).

.removeAt(idx)      -> val | nil

- Removes element at 'idx' and returns it. Subsequent elements shift left.
- Returns 'nil' if index is out of bounds.
- O(N).

.clear()            -> void

- Removes all elements.
- Does NOT release internal capacity (reserved for v1.1 .shrink()).
- O(1) logic / O(N) destruction.

.slice(start, end)  -> list

- Returns a NEW list containing elements from 'start' to 'end' (exclusive).
- Deep copy of references (no structural sharing).
- O(N).

.join(sep)          -> string

- Converts all elements to strings and concatenates them with 'sep'.
- O(N).

================================================================================

2. Map (Hash-backed)
   ================================================================================
   [Syntax]
   map[key] = val -> Handled by OP_SETINDEX (VM intrinsic).
   val = map[key]      -> Handled by OP_GETINDEX (VM intrinsic).

[Properties]
.size -> int

- Read-only property.
- Returns the number of key-value pairs.
- O(1).

[Methods]
.has(key)           -> bool

- Checks if 'key' exists, regardless of whether value is nil.
- O(1).

.remove(key)        -> val | nil

- Removes the entry for 'key' and returns its value.
- Returns 'nil' if key did not exist.
- O(1).

.clear()            -> void

- Removes all entries.
- O(N).

.keys()             -> list

- Allocates and returns a NEW list containing all keys.
- Order is implementation-dependent (usually insertion order or hash order).
- WARNING: High memory cost for large maps.
- O(N).

.values()           -> list

- Allocates and returns a NEW list containing all values.
- O(N).

================================================================================

3. String (Immutable UTF-8 Bytes)
   ================================================================================
   [Properties]
   .length -> int
   - Read-only property.
   - Returns the number of BYTES (not characters/runes).
   - O(1).

[Methods]
.slice(start, end)  -> string

- Returns a NEW string from byte offset 'start' to 'end' (exclusive).
- Does NOT validation UTF-8 boundaries (user responsibility).
- O(N).

.find(sub)          -> int

- Returns the first byte offset of 'sub'.
- Returns -1 if not found.
- O(N*M).

.contains(sub)      -> bool

- Returns true if 'sub' is found.
- O(N*M).

.replace(old, new)  -> string

- Replaces ALL occurrences of 'old' with 'new'.
- Returns a NEW string.
- O(N).

.split(sep)         -> list<string>

- Splits the string by 'sep' delimiter.
- Returns a list of new strings.
- O(N).

.trim()             -> string

- Removes ASCII whitespace (space, tab, newline) from both ends.
- Returns a new string.
- O(N).

================================================================================
End of Specification