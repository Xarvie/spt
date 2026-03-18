# SPT语言AST测试覆盖计划

## 目标

覆盖LangParser.g4中定义的所有AST节点类型，为每种类型创建独立的测试目录，包含各种组合的测试用例，确保AI能严格按照g4语法实现正确的代码。

---

## 目录结构规划

```
test/
├── 01_statements/           # 语句测试
│   ├── empty_statement/     # 空语句
│   ├── assignment/          # 赋值语句
│   ├── update_assignment/   # 更新赋值语句
│   ├── expression_stmt/     # 表达式语句
│   ├── block/               # 代码块语句
│   ├── break_continue/      # break/continue语句
│   ├── return_stmt/         # return语句
│   └── defer_stmt/          # defer语句
│
├── 02_declarations/         # 声明测试
│   ├── variable/            # 变量声明
│   ├── constant/            # 常量声明
│   ├── global/              # 全局变量声明
│   ├── function/            # 函数声明
│   ├── multi_return_func/   # 多返回值函数
│   └── export_decl/         # 导出声明
│
├── 03_classes/              # 类测试
│   ├── basic_class/         # 基本类定义
│   ├── fields/              # 字段成员
│   ├── methods/             # 方法成员
│   ├── static_members/      # 静态成员
│   ├── const_members/       # const成员
│   └── class_instantiation/ # 类实例化
│
├── 04_control_flow/         # 控制流测试
│   ├── if_else/             # if/else语句
│   ├── while_loop/          # while循环
│   ├── for_numeric/         # 数值for循环
│   └── for_each/            # 泛型for-each循环
│
├── 05_expressions/          # 表达式测试
│   ├── arithmetic/          # 算术运算 (+, -, *, /, ~/, %)
│   ├── comparison/          # 比较运算 (<, >, <=, >=, ==, !=)
│   ├── logical/             # 逻辑运算 (&&, ||, !)
│   ├── bitwise/             # 位运算 (&, |, ^, ~, <<, >>)
│   ├── string_concat/       # 字符串连接 (..)
│   ├── unary/               # 一元运算 (-, #, ~, !)
│   ├── postfix/             # 后缀运算 ([], ., ())
│   └── parentheses/         # 括号表达式
│
├── 06_literals/             # 字面量测试
│   ├── null_literal/        # null字面量
│   ├── boolean_literal/     # 布尔字面量
│   ├── integer_literal/     # 整数字面量
│   ├── float_literal/       # 浮点字面量
│   ├── string_literal/      # 字符串字面量
│   ├── list_literal/        # List字面量
│   └── map_literal/         # Map字面量
│
├── 07_types/                # 类型系统测试
│   ├── primitive_types/     # 基本类型
│   ├── list_type/           # List类型
│   ├── map_type/            # Map类型
│   ├── any_type/            # any类型
│   └── auto_type/           # auto类型推断
│
├── 08_lambda/               # Lambda表达式测试
│   ├── basic_lambda/        # 基本lambda
│   ├── lambda_with_params/  # 带参数lambda
│   ├── lambda_closure/      # 闭包
│   └── lambda_as_arg/       # lambda作为参数
│
├── 09_import_export/        # 导入导出测试
│   ├── import_namespace/    # import * as
│   ├── import_named/        # import { a, b }
│   ├── import_alias/        # import { a as b }
│   └── export_module/       # export导出
│
├── 10_operators/            # 运算符测试
│   ├── assign_ops/          # 赋值运算符 (=, +=, -=, etc.)
│   ├── precedence/          # 运算符优先级
│   └── associativity/       # 运算符结合性
│
└── 11_edge_cases/           # 边界情况测试
    ├── nested_expressions/  # 嵌套表达式
    ├── complex_types/       # 复杂类型嵌套
    ├── unicode_identifiers/ # Unicode标识符
    └── escape_sequences/    # 转义序列
```

---

## 详细测试用例规划

### 1. 语句测试 (01_statements)

#### 1.1 empty_statement/

- `empty_semicolon.spt` - 单独的分号语句
- `multiple_empty.spt` - 多个连续空语句
- `empty_in_block.spt` - 代码块中的空语句

#### 1.2 assignment/

- `simple_assign.spt` - 简单赋值 `a = 1;`
- `multi_target_assign.spt` - 多目标赋值 `a, b = 1, 2;`
- `index_assign.spt` - 索引赋值 `arr[0] = 1;`
- `member_assign.spt` - 成员赋值 `obj.field = 1;`
- `chain_assign.spt` - 链式赋值验证

#### 1.3 update_assignment/

- `add_assign.spt` - `+=` 运算符
- `sub_assign.spt` - `-=` 运算符
- `mul_assign.spt` - `*=` 运算符
- `div_assign.spt` - `/=` 运算符
- `idiv_assign.spt` - `~/=` 运算符
- `mod_assign.spt` - `%=` 运算符
- `concat_assign.spt` - `..=` 运算符

#### 1.4 expression_stmt/

- `func_call_stmt.spt` - 函数调用作为语句
- `method_call_stmt.spt` - 方法调用作为语句
- `literal_expr.spt` - 字面量表达式语句

#### 1.5 block/

- `simple_block.spt` - 简单代码块
- `nested_block.spt` - 嵌套代码块
- `block_with_vars.spt` - 代码块中的变量作用域

#### 1.6 break_continue/

- `break_in_while.spt` - while中的break
- `break_in_for.spt` - for中的break
- `continue_in_while.spt` - while中的continue
- `continue_in_for.spt` - for中的continue
- `nested_loop_control.spt` - 嵌套循环中的控制

#### 1.7 return_stmt/

- `return_void.spt` - 无返回值return
- `return_single.spt` - 单返回值
- `return_multi.spt` - 多返回值
- `return_expression.spt` - 返回表达式

#### 1.8 defer_stmt/

- `defer_basic.spt` - 基本defer
- `defer_order.spt` - 多个defer执行顺序
- `defer_with_return.spt` - defer与return交互

---

### 2. 声明测试 (02_declarations)

#### 2.1 variable/

- `typed_var.spt` - 带类型变量声明
- `auto_var.spt` - auto类型推断
- `var_with_init.spt` - 带初始化的变量
- `var_no_init.spt` - 不带初始化的变量

#### 2.2 constant/

- `const_basic.spt` - const变量声明
- `const_with_type.spt` - 带类型的const
- `const_expression.spt` - const初始化表达式

#### 2.3 global/

- `global_var.spt` - global变量声明
- `global_const.spt` - global const声明
- `global_in_function.spt` - 函数内声明global

#### 2.4 function/

- `func_no_params.spt` - 无参数函数
- `func_with_params.spt` - 带参数函数
- `func_with_return.spt` - 有返回值函数
- `func_void_return.spt` - void返回函数
- `func_variadic.spt` - 可变参数函数
- `func_qualified_name.spt` - 限定名函数

#### 2.5 multi_return_func/

- `multi_return_basic.spt` - 基本多返回值
- `multi_return_call.spt` - 调用多返回值函数

#### 2.6 export_decl/

- `export_var.spt` - 导出变量
- `export_func.spt` - 导出函数
- `export_class.spt` - 导出类 (已存在)

---

### 3. 类测试 (03_classes)

#### 3.1 basic_class/

- `empty_class.spt` - 空类
- `class_with_semicolon.spt` - 只有分号的类
- `simple_class.spt` - 简单类定义

#### 3.2 fields/

- `instance_field.spt` - 实例字段
- `field_with_init.spt` - 带初始化的字段
- `field_no_init.spt` - 不带初始化的字段
- `multiple_fields.spt` - 多个字段

#### 3.3 methods/

- `instance_method.spt` - 实例方法
- `method_with_params.spt` - 带参数方法
- `method_with_return.spt` - 有返回值方法
- `method_multi_return.spt` - 多返回值方法
- `constructor.spt` - 构造函数(__init)

#### 3.4 static_members/

- `static_field.spt` - 静态字段
- `static_method.spt` - 静态方法
- `static_const.spt` - 静态常量

#### 3.5 const_members/

- `const_field.spt` - const字段
- `const_method.spt` - const方法

#### 3.6 class_instantiation/

- `new_basic.spt` - 基本new表达式
- `new_with_args.spt` - 带参数new
- `new_qualified.spt` - 限定名new (Module.Class)

---

### 4. 控制流测试 (04_control_flow)

#### 4.1 if_else/

- `if_only.spt` - 只有if
- `if_else.spt` - if-else
- `if_elseif_else.spt` - if-elseif-else链
- `nested_if.spt` - 嵌套if
- `if_with_block.spt` - 带代码块的if

#### 4.2 while_loop/

- `while_basic.spt` - 基本while
- `while_with_break.spt` - while中break
- `while_with_continue.spt` - while中continue
- `nested_while.spt` - 嵌套while

#### 4.3 for_numeric/

- `for_basic.spt` - 基本数值for
- `for_with_step.spt` - 带步长for
- `for_typed_var.spt` - 带类型for变量
- `for_auto_var.spt` - auto类型for变量
- `for_untyped_var.spt` - 无类型for变量

#### 4.4 for_each/

- `for_each_single.spt` - 单变量for-each
- `for_each_kv.spt` - 键值对for-each
- `for_each_typed.spt` - 带类型for-each
- `for_each_ipairs.spt` - ipairs迭代
- `for_each_pairs.spt` - pairs迭代

---

### 5. 表达式测试 (05_expressions)

#### 5.1 arithmetic/

- `add_sub.spt` - 加减法
- `mul_div.spt` - 乘除法
- `int_div.spt` - 整数除法(~/)
- `mod.spt` - 取模
- `mixed_arithmetic.spt` - 混合运算

#### 5.2 comparison/

- `equal_not_equal.spt` - ==, !=
- `less_greater.spt` - <, >, <=, >=
- `chained_comparison.spt` - 链式比较

#### 5.3 logical/

- `and_or.spt` - &&, ||
- `not.spt` - !运算
- `short_circuit.spt` - 短路求值

#### 5.4 bitwise/

- `bit_and_or.spt` - &, |
- `bit_xor.spt` - ^运算
- `bit_not.spt` - ~运算
- `left_shift.spt` - <<运算
- `right_shift.spt` - >>运算

#### 5.5 string_concat/

- `basic_concat.spt` - 基本..连接
- `multi_concat.spt` - 多个..连接
- `concat_with_literal.spt` - 与字面量连接

#### 5.6 unary/

- `negate.spt` - 负号-
- `length.spt` - 长度#
- `bit_not_unary.spt` - 位非~
- `not_unary.spt` - 逻辑非!

#### 5.7 postfix/

- `index_access.spt` - 索引访问[]
- `member_access.spt` - 成员访问.
- `func_call.spt` - 函数调用()
- `method_call.spt` - 方法调用
- `chained_postfix.spt` - 链式后缀

#### 5.8 parentheses/

- `simple_paren.spt` - 简单括号
- `nested_paren.spt` - 嵌套括号
- `paren_precedence.spt` - 括号改变优先级

---

### 6. 字面量测试 (06_literals)

#### 6.1 null_literal/

- `null_assign.spt` - null赋值
- `null_compare.spt` - null比较

#### 6.2 boolean_literal/

- `true_false.spt` - true/false使用

#### 6.3 integer_literal/

- `decimal_int.spt` - 十进制整数
- `hex_int.spt` - 十六进制整数

#### 6.4 float_literal/

- `basic_float.spt` - 基本浮点数
- `scientific_float.spt` - 科学计数法
- `hex_float.spt` - 十六进制浮点数

#### 6.5 string_literal/

- `double_quote.spt` - 双引号字符串
- `single_quote.spt` - 单引号字符串
- `escape_sequences.spt` - 转义序列
- `unicode_escape.spt` - Unicode转义

#### 6.6 list_literal/

- `empty_list.spt` - 空列表
- `simple_list.spt` - 简单列表
- `nested_list.spt` - 嵌套列表
- `mixed_list.spt` - 混合类型列表

#### 6.7 map_literal/

- `empty_map.spt` - 空Map
- `ident_key_map.spt` - 标识符键
- `string_key_map.spt` - 字符串键
- `int_key_map.spt` - 整数键
- `expr_key_map.spt` - 表达式键
- `nested_map.spt` - 嵌套Map

---

### 7. 类型系统测试 (07_types)

#### 7.1 primitive_types/

- `int_type.spt` - int类型
- `float_type.spt` - float类型
- `string_type.spt` - string类型
- `bool_type.spt` - bool类型
- `void_type.spt` - void类型
- `null_type.spt` - null类型
- `coro_type.spt` - coro类型
- `function_type.spt` - function类型

#### 7.2 list_type/

- `list_no_generic.spt` - 无泛型list
- `list_generic.spt` - 泛型list<int>
- `nested_list_type.spt` - 嵌套list<list<int>>

#### 7.3 map_type/

- `map_no_generic.spt` - 无泛型map
- `map_generic.spt` - 泛型map<string, int>
- `nested_map_type.spt` - 嵌套map类型

#### 7.4 any_type/

- `any_variable.spt` - any类型变量
- `any_parameter.spt` - any类型参数

#### 7.5 auto_type/

- `auto_infer.spt` - auto类型推断
- `auto_with_literal.spt` - auto与字面量
- `auto_with_expression.spt` - auto与表达式

---

### 8. Lambda测试 (08_lambda)

#### 8.1 basic_lambda/

- `lambda_no_params.spt` - 无参数lambda
- `lambda_void_return.spt` - void返回lambda
- `lambda_typed_return.spt` - 类型返回lambda

#### 8.2 lambda_with_params/

- `lambda_single_param.spt` - 单参数lambda
- `lambda_multi_params.spt` - 多参数lambda
- `lambda_variadic.spt` - 可变参数lambda

#### 8.3 lambda_closure/

- `simple_closure.spt` - 简单闭包
- `nested_closure.spt` - 嵌套闭包

#### 8.4 lambda_as_arg/

- `lambda_callback.spt` - lambda作为回调
- `lambda_higher_order.spt` - 高阶函数

---

### 9. 导入导出测试 (09_import_export)

#### 9.1 import_namespace/

- `import_all.spt` - import * as name
- `use_namespace.spt` - 使用命名空间成员

#### 9.2 import_named/

- `import_single.spt` - import { a }
- `import_multiple.spt` - import { a, b, c }

#### 9.3 import_alias/

- `import_with_alias.spt` - import { a as b }

#### 9.4 export_module/

- `export_var.spt` - export变量
- `export_func.spt` - export函数
- `export_multiple.spt` - export多个

---

### 10. 运算符测试 (10_operators)

#### 10.1 assign_ops/

- `all_assign_ops.spt` - 所有赋值运算符

#### 10.2 precedence/

- `arithmetic_precedence.spt` - 算术优先级
- `logical_precedence.spt` - 逻辑优先级
- `mixed_precedence.spt` - 混合优先级

#### 10.3 associativity/

- `left_associative.spt` - 左结合运算符
- `right_associative.spt` - 右结合运算符

---

### 11. 边界情况测试 (11_edge_cases)

#### 11.1 nested_expressions/

- `deeply_nested.spt` - 深度嵌套表达式
- `mixed_nested.spt` - 混合嵌套

#### 11.2 complex_types/

- `list_of_map.spt` - list<map<string, int>>
- `map_of_list.spt` - map<string, list<int>>

#### 11.3 unicode_identifiers/

- `chinese_ident.spt` - 中文标识符
- `emoji_ident.spt` - Emoji标识符

#### 11.4 escape_sequences/

- `all_escapes.spt` - 所有转义序列
- `unicode_in_string.spt` - 字符串中的Unicode

---

## 测试文件命名规范

1. 每个测试文件以 `.spt` 为扩展名
2. 文件名使用下划线分隔，如 `multi_return_func.spt`
3. 每个测试文件应包含注释说明测试目的
4. 测试代码应简洁明了，覆盖特定语法点

## 测试用例模板

```spt
// 测试: [测试类型] - [具体测试点]
// 语法: [对应的g4语法规则]
// 描述: [测试目的说明]

// 测试代码
...
```

## 实施优先级

1. **高优先级** - 核心语法特性
    - 语句、声明、表达式、控制流

2. **中优先级** - 类型系统和类
    - 类型注解、类定义、成员访问

3. **低优先级** - 高级特性和边界情况
    - Lambda、复杂嵌套、Unicode

---

## 预估测试文件数量

| 目录               | 预估文件数    |
|------------------|----------|
| 01_statements    | ~25      |
| 02_declarations  | ~20      |
| 03_classes       | ~20      |
| 04_control_flow  | ~20      |
| 05_expressions   | ~35      |
| 06_literals      | ~25      |
| 07_types         | ~20      |
| 08_lambda        | ~12      |
| 09_import_export | ~10      |
| 10_operators     | ~10      |
| 11_edge_cases    | ~10      |
| **总计**           | **~207** |

---

## 下一步行动

1. 创建测试目录结构
2. 按优先级逐步实现测试用例
3. 每个测试用例确保符合g4语法规范
4. 建立测试验证机制
