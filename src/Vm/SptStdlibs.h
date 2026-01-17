#pragma once
#include "Value.h"
#include <string>

/*
# SPT 标准库 API

## Global Functions

### 输出

`print(...args)`
打印输出到控制台。args: 任意数量任意类型的参数，用空格分隔

### 类型转换

`toInt(value)` → int
转换为整数。value: 数字、字符串或布尔值，转换失败返回 0

`toFloat(value)` → float
转换为浮点数。value: 数字、字符串或布尔值，转换失败返回 0.0

`toString(value)` → string
转换为字符串。value: 任意类型

`toBool(value)` → bool
转换为布尔值。value: 任意类型，遵循真值性规则

### 类型检查

`typeOf(value)` → string
返回值的类型名称。可能的返回值: "nil", "bool", "int", "float", "string", "list", "map", "instance",
"function", "class", "native", "fiber", "native_class", "native_instance"

`isInt(value)` → bool
检查是否为整数类型

`isFloat(value)` → bool
检查是否为浮点数类型

`isNumber(value)` → bool
检查是否为数字类型（整数或浮点数）

`isString(value)` → bool
检查是否为字符串类型

`isBool(value)` → bool
检查是否为布尔类型

`isList(value)` → bool
检查是否为列表类型

`isMap(value)` → bool
检查是否为映射类型

`isNull(value)` → bool
检查是否为 nil

`isFunction(value)` → bool
检查是否为函数（闭包或原生函数）

### 数学函数

`abs(number)` → number
返回绝对值。number: 整数或浮点数

`floor(number)` → int
向下取整。number: 数字，结果在 int64 范围内返回整数，否则返回浮点数

`ceil(number)` → int
向上取整。number: 数字，结果在 int64 范围内返回整数，否则返回浮点数

`round(number)` → int
四舍五入。number: 数字，结果在 int64 范围内返回整数，否则返回浮点数

`sqrt(number)` → float
返回平方根。number: 数字，非数字类型抛出错误

`pow(base, exp)` → float
返回 base 的 exp 次幂。base: 底数，exp: 指数，均须为数字

`min(a, b)` → number
返回两个数的最小值。a, b: 数字，两者均为整数时返回整数

`max(a, b)` → number
返回两个数的最大值。a, b: 数字，两者均为整数时返回整数

### 字符函数

`char(code)` → string
将 ASCII 码转换为单字符字符串。code: 0-127 的整数，超出范围返回空字符串

`ord(char)` → int
返回字符串首字符的 ASCII 码。char: 字符串，空字符串返回 0

### 工具函数

`len(value)` → int
返回长度。value: 字符串返回字符数，列表返回元素数，映射返回键值对数，其他类型返回 0

`range(start, end, step?)` → list
生成数字序列列表。start: 起始值（包含），end: 结束值（不包含），step: 步长（可选，默认为 1，不能为
0）

`pairs(collection)` → iterator
返回集合的迭代器，用于 for-each 循环。collection: 列表或映射

`apply(fn, argList?, receiver?)` → value
使用列表中的元素作为参数调用函数。fn: 要调用的函数（闭包或原生函数）, receiver：可选，args:
可选,参数列表

### 错误处理

`assert(condition, message?)`
断言条件为真，否则抛出运行时错误。condition: 布尔表达式，message: 可选的错误消息

`error(message?)`
抛出错误。message: 错误值（可选，任意类型）

`pcall(fn, ...args)` → (bool, ...values)
保护调用函数，捕获错误而非中断执行。fn: 要调用的函数，args: 传递给函数的参数
返回值: 成功时返回 (true, 返回值...)，失败时返回 (false, 错误值)

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
  static bool getProperty(VM *vm, Value object, std::string_view fieldName, Value &outValue);

  // 为object设置property或method
  static bool setProperty(VM *vm, Value object, std::string_view fieldName, const Value &value);

  // 调用object的方法
  static bool invokeMethod(VM *vm, Value receiver, std::string_view methodName, int argc,
                           Value *argv, Value &outResult);
};

} // namespace spt
