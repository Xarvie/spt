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
字符串字符数量（Code Points，只读属性）。注意：需扫描 UTF-8 序列 (O(N))

`byteLength`
字符串底层字节长度（只读属性）。返回实际内存占用大小 (O(1))

`slice(start, end)` → string
返回子串。基于字符索引。start: 起始索引（包含），end: 结束索引（不包含），支持负索引

`indexOf(substring)` → int
返回子串首次出现的字符索引。substring: 要查找的字符串，未找到返回 -1

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
按分隔符分割成列表。delimiter: 分隔字符串，若为空字符串则按字符拆分

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

## Bytes

可变长度字节数组，提供类似 DataView 的二进制读写能力，支持大小端字节序。
`length`
字节数组长度（只读属性）

`Bytes.create(size)` → Bytes
创建指定大小的字节数组，初始值为 0。size: 非负整数

`Bytes.fromList(list)` → Bytes
从整数列表创建字节数组。list: 整数列表，每个元素取低 8 位

`Bytes.fromStr(str)` → Bytes
从字符串创建字节数组（UTF-8 编码）。str: 字符串

`Bytes.fromHex(hexStr)` → Bytes
从十六进制字符串创建字节数组。hexStr: 十六进制字符串，可含空格，长度必须为偶数

`push(byte)`
添加字节到末尾。byte: 整数，取低 8 位

`pop()` → int | nil
移除并返回最后一个字节，空数组返回 nil

`clear()`
清空字节数组

`resize(newSize)`
调整数组大小。newSize: 非负整数，扩展部分填充 0

`slice(start, end)` → Bytes
返回子数组。start: 起始索引（包含），end: 结束索引（不包含），支持负索引

`fill(value, start, end)`
填充指定范围。value: 填充值（取低 8 位），start: 起始索引，end: 结束索引

`readInt8(offset)` → int
读取有符号 8 位整数。offset: 字节偏移量

`readUInt8(offset)` → int
读取无符号 8 位整数。offset: 字节偏移量

`readInt16(offset, bigEndian?)` → int
读取有符号 16 位整数。offset: 字节偏移量，bigEndian: 大端序（默认 false）

`readUInt16(offset, bigEndian?)` → int
读取无符号 16 位整数。offset: 字节偏移量，bigEndian: 大端序（默认 false）

`readInt32(offset, bigEndian?)` → int
读取有符号 32 位整数。offset: 字节偏移量，bigEndian: 大端序（默认 false）

`readUInt32(offset, bigEndian?)` → int
读取无符号 32 位整数。offset: 字节偏移量，bigEndian: 大端序（默认 false）

`readFloat(offset, bigEndian?)` → float
读取 32 位浮点数。offset: 字节偏移量，bigEndian: 大端序（默认 false）

`readDouble(offset, bigEndian?)` → float
读取 64 位浮点数。offset: 字节偏移量，bigEndian: 大端序（默认 false）

`readString(offset, length)` → string
读取指定长度的字符串。offset: 字节偏移量，length: 读取字节数

`writeInt8(offset, value)`
写入有符号 8 位整数。offset: 字节偏移量，value: 整数值

`writeUInt8(offset, value)`
写入无符号 8 位整数。offset: 字节偏移量，value: 整数值（取低 8 位）

`writeInt16(offset, value, bigEndian?)`
写入有符号 16 位整数。offset: 字节偏移量，value: 整数值，bigEndian: 大端序（默认 false）

`writeUInt16(offset, value, bigEndian?)`
写入无符号 16 位整数。offset: 字节偏移量，value: 整数值，bigEndian: 大端序（默认 false）

`writeInt32(offset, value, bigEndian?)`
写入有符号 32 位整数。offset: 字节偏移量，value: 整数值，bigEndian: 大端序（默认 false）

`writeUInt32(offset, value, bigEndian?)`
写入无符号 32 位整数。offset: 字节偏移量，value: 整数值，bigEndian: 大端序（默认 false）

`writeFloat(offset, value, bigEndian?)`
写入 32 位浮点数。offset: 字节偏移量，value: 数字，bigEndian: 大端序（默认 false）

`writeDouble(offset, value, bigEndian?)`
写入 64 位浮点数。offset: 字节偏移量，value: 数字，bigEndian: 大端序（默认 false）

`writeString(offset, str)` → int
写入字符串，返回实际写入字节数。offset: 字节偏移量，str: 字符串

`toStr()` → string
将字节数组转换为字符串（UTF-8 解码）

`toHex()` → string
将字节数组转换为大写十六进制字符串
*/

namespace spt {
class VM;
struct StringObject;

class StdlibDispatcher {
public:
  // 获取属性或绑定方法（使用 StringObject* 键）
  static bool getProperty(VM *vm, Value object, StringObject *fieldName, Value &outValue);

  // 直接调用方法（使用 StringObject* 键）
  static bool invokeMethod(VM *vm, Value receiver, StringObject *methodName, int argc, Value *argv,
                           Value &outResult);

  // 设置属性（使用 StringObject* 键）
  static bool setProperty(VM *vm, Value object, StringObject *fieldName, const Value &value);

  // === 兼容性 API（仅用于非热路径） ===
  // 这些版本会通过 VM 驻留字符串后调用指针版本
  static bool getProperty(VM *vm, Value object, std::string_view fieldName, Value &outValue);
  static bool invokeMethod(VM *vm, Value receiver, std::string_view methodName, int argc,
                           Value *argv, Value &outResult);
  static bool setProperty(VM *vm, Value object, std::string_view fieldName, const Value &value);
};

} // namespace spt