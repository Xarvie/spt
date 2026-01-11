#pragma once
#include "Value.h"
#include <string>

/*
# SPT 标准库 API

## List

`length`
列表长度（只读属性）

`push(value)`
添加元素到末尾。value: 任意类型

`pop()` → value
移除并返回最后一个元素，空列表返回 nil

`insert(index, value)`
在指定位置插入元素。index: 整数索引，value: 任意类型

`clear()`
清空列表所有元素

`removeAt(index)` → value
移除并返回指定位置的元素。index: 整数索引，越界返回 nil

`indexOf(value)` → int
返回元素首次出现的索引。value: 要查找的元素，未找到返回 -1

`contains(value)` → bool
检查列表是否包含元素。value: 要检查的元素

`slice(start, end)` → list
返回子列表。start: 起始索引（包含），end: 结束索引（不包含），支持负索引

`join(separator?)` → string
用分隔符连接所有元素。separator: 分隔字符串，默认为空

## Map

`size`
键值对数量（只读属性）

`has(key)` → bool
检查键是否存在。key: 要检查的键

`clear()`
清空映射所有键值对

`keys()` → list
返回所有键组成的列表

`values()` → list
返回所有值组成的列表

`remove(key)` → value
移除并返回指定键的值。key: 要移除的键，不存在返回 nil

## String

`length`
字符串长度（只读属性）

`slice(start, end)` → string
返回子串。start: 起始索引（包含），end: 结束索引（不包含），支持负索引

`indexOf(substring)` → int
返回子串首次出现的位置。substring: 要查找的字符串，未找到返回 -1

`find(substring)` → int
同 indexOf。substring: 要查找的字符串

`contains(substring)` → bool
检查是否包含子串。substring: 要检查的字符串

`startsWith(prefix)` → bool
检查是否以指定前缀开头。prefix: 前缀字符串

`endsWith(suffix)` → bool
检查是否以指定后缀结尾。suffix: 后缀字符串

`toUpper()` → string
返回大写转换后的新字符串

`toLower()` → string
返回小写转换后的新字符串

`trim()` → string
返回去除首尾空白字符后的新字符串

`split(delimiter?)` → list
按分隔符分割成列表。delimiter: 分隔字符串，默认按单字符分割

`replace(old, new)` → string
替换所有匹配项。old: 要替换的子串，new: 替换后的子串

## Fiber

`isDone` → bool
协程是否已完成（只读属性）

`error` → value
协程的错误值，无错误返回 nil（只读属性）

`call(value?)` → value
调用或恢复协程执行。value: 传递给协程的值，可选

`try(value?)` → value
安全调用协程，出错返回错误值而非抛出。value: 传递给协程的值，可选
*/

namespace spt {
class VM;

class StdlibDispatcher {
public:
  // 从object中获取property或method
  static bool getProperty(VM *vm, Value object, const std::string &fieldName, Value &outValue);

  // 为object设置property或method
  static bool setProperty(VM *vm, Value object, const std::string &fieldName, const Value &value);

  // 调用object的方法
  static bool invokeMethod(VM *vm, Value receiver, const std::string &methodName, int argc,
                           Value *argv, Value &outResult);
};

} // namespace spt
