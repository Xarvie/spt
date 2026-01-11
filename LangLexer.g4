lexer grammar LangLexer; // 定义词法分析器 LangLexer

// --- 关键字 (Keywords) ---
// 类型相关
INT: 'int';             // 整数类型
FLOAT: 'float';         // 浮点数类型
NUMBER: 'number';       // 通用数字类型提示
STRING: 'string';       // 字符串类型
BOOL: 'bool';           // 布尔类型
ANY: 'any';             // 任意类型
VOID: 'void';           // void 类型 (用于函数返回值)
NULL_: 'null';           // null 空值
LIST: 'list';           // list 容器类型
MAP: 'map';             // map 容器类型
FUNCTION: 'function';   // function 类型 (用于 lambda 定义和类型提示)
COROUTINE: 'coro'; // 协程类型
MUTIVAR: 'mutivar';     // 多返回值

// 控制流
IF: 'if';
ELSE: 'else';
WHILE: 'while';
FOR: 'for';
BREAK: 'break';
CONTINUE: 'continue';
RETURN: 'return';
DEFER: 'defer';

// 字面量与常量
TRUE: 'true';
FALSE: 'false';
CONST: 'const'; // 常量意图声明 (提示作用)

// 声明与作用域
AUTO: 'auto';           // 自动类型推断
GLOBAL: 'global';       // 全局作用域声明
STATIC: 'static';       // 类静态成员/方法
IMPORT: 'import';       // 模块导入提示 (编译时)
AS:     'as';           // 模块导入别名
TYPE:   'type';
FROM:   'from';         // 用于 import { ... } from "..."
PRIVATE: 'private';     // 私有访问修饰符提示 (静态分析用)
EXPORT: 'export';

// 面向对象
CLASS: 'class';         // 类定义
NEW: 'new';             // 创建实例

// --- 内建函数名作为 IDENTIFIER 处理 ---
// PRINT, ERROR, PCALL, XPCALL, REQUIRE, TYPEOF 等现在是标识符

// --- 运算符 (Operators) ---
ADD: '+';               // 加法
SUB: '-';               // 减法 / 一元负号
MUL: '*';               // 乘法
DIV: '/';               // 除法
IDIV: '~/';             // 整数除法
MOD: '%';               // 取模

ASSIGN: '=';            // 赋值
ADD_ASSIGN: '+=';       // 加法赋值
SUB_ASSIGN: '-=';       // 减法赋值
MUL_ASSIGN: '*=';       // 乘法赋值
DIV_ASSIGN: '/=';       // 除法赋值
IDIV_ASSIGN: '~/=';     // 整数除法赋值
MOD_ASSIGN: '%=';       // 取模赋值
CONCAT_ASSIGN: '..=';   // 字符串连接赋值

EQ: '==';               // 等于
NEQ: '!=';              // 不等于
LT: '<';                // 小于
GT: '>';                // 大于
LTE: '<=';              // 小于等于
GTE: '>=';              // 大于等于

AND: '&&';              // 逻辑与 (短路)
OR: '||';               // 逻辑或 (短路)
NOT: '!';               // 逻辑非

CONCAT: '..';           // 字符串连接
LEN: '#';               // 长度运算符 (list 元素个数 / string 字节数)

BIT_AND: '&';           // 按位与
BIT_OR: '|';            // 按位或
BIT_XOR: '^';           // 按位异或
BIT_NOT: '~';           // 按位非
LSHIFT: '<<';           // 左移
//RSHIFT: '>>';  无此Token，转移到Parser内解决 list<list<int>> 的冲突问题
ARROW: '->';            // 用于 Lambda 返回类型

// --- 分隔符 (Delimiters) ---
OP: '(';                // 开圆括号
CP: ')';                // 闭圆括号
OSB: '[';               // 开方括号
CSB: ']';               // 闭方括号
OCB: '{';               // 开花括号
CCB: '}';               // 闭花括号
COMMA: ',';             // 逗号
DOT: '.';               // 点号
COL: ':';               // 冒号
SEMICOLON: ';';         // 分号
DDD: '...';             // 变长参数

// --- 字面量 (Literals) ---
INTEGER: DIGIT+ | '0' [xX] HEX_DIGIT+; // 整型 (十进制, 十六进制)
FLOAT_LITERAL                     // 浮点型 (多种格式)
    : DIGIT+ '.' DIGIT* EXPONENT?
    | '.' DIGIT+ EXPONENT?
    | DIGIT+ EXPONENT
    | '0' [xX] HEX_DIGIT+ '.' HEX_DIGIT* HEX_EXPONENT?
    | '0' [xX] '.' HEX_DIGIT+ HEX_EXPONENT?
    | '0' [xX] HEX_DIGIT+ HEX_EXPONENT
    ;
STRING_LITERAL                     // 字符串 (双引号或单引号)
    : '"' ( EscapeSequence | ~["\\] )* '"'
    | '\'' ( EscapeSequence | ~['\\] )* '\''
    ;

// --- 标识符 (Identifiers) ---
IDENTIFIER
    : [a-zA-Z_][a-zA-Z_0-9]* // 关键字必须在此规则之前定义
    ;

// --- 空白与注释 (Whitespace and Comments) ---
WS: [ \t\r\n]+ -> skip;             // 跳过空白字符
LINE_COMMENT: '//' ~[\r\n]* -> skip; // 跳过单行注释
BLOCK_COMMENT: '/*' .*? '*/' -> skip; // 跳过多行注释

// --- 片段规则 (Fragment Rules - 辅助词法规则) ---
fragment DIGIT: [0-9];             // 十进制数字
fragment HEX_DIGIT: [0-9a-fA-F];   // 十六进制数字
fragment EXPONENT: [eE] [+\-]? DIGIT+; // 科学计数法指数 (十进制)
fragment HEX_EXPONENT: [pP] [+\-]? DIGIT+; // 科学计数法指数 (十六进制)
fragment EscapeSequence             // 转义序列
    : '\\' [btnfr"']               // 基本转义字符 (\b, \t, \n, \f, \r, \", \')
    | '\\' '\\'                   // 反斜杠本身 (\\)
    | UnicodeEscape               // Unicode 转义 (\u{...})
    | HexEscape                   // 十六进制字节转义 (\xHH)
    ;
fragment UnicodeEscape: '\\u{' HEX_DIGIT+ '}'; // 例如: \u{1F60A}
fragment HexEscape: '\\x' HEX_DIGIT HEX_DIGIT; // 例如: \x0A