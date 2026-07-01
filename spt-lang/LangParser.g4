parser grammar LangParser;

options { tokenVocab=LangLexer; } // 使用 LangLexer 定义的词法单元

// --- 入口规则 (Entry Point) ---
/** 编译单元：整个文件的内容，由代码块和文件结束符组成 */
compilationUnit
    : statement* EOF
    ;

// --- 核心语句 ---
/** 代码块：包含零个或多个语句 */
blockStatement
    : '{' statement* '}'
    ;

/** 单个语句的定义 */
statement
    : SEMICOLON                            #semicolonStmt     // 空语句
    | assignStatement SEMICOLON          #assignStmt        // 普通赋值语句 (a = b;)
    | updateStatement SEMICOLON          #updateStmt        // 更新赋值语句 (a += b;)
    | expression SEMICOLON               #expressionStmt    // 表达式
    | declaration              #declarationStmt   //  // 变量声明语句分号在内部实现
    | ifStatement                         #ifStmt            // If 语句
    | whileStatement                      #whileStmt         // While 语句
    | forStatement                        #forStmt           // For 语句
    | BREAK SEMICOLON                    #breakStmt         // Break 语句
    | CONTINUE SEMICOLON                 #continueStmt      // Continue 语句
    | RETURN expressionList? SEMICOLON       #returnStmt        // Return 语句
    | blockStatement                     #blockStmt         // 显式代码块 {...}
    | importStatement SEMICOLON #importStmt
    | deferStatement                       #deferStmt
    | ambientDeclaration                   #ambientDeclStmt   // 环境声明 (顶层外部符号，编译期擦除)
    | declareModule                        #declareModuleStmt // 模块声明块 (描述外部模块形状)
    ;

//-- 导入语句 --
importStatement
    : IMPORT MUL AS IDENTIFIER FROM STRING_LITERAL               #importNamespaceStmt
    | IMPORT OCB importSpecifier (COMMA importSpecifier)* CCB FROM STRING_LITERAL #importNamedStmt
    ;

importSpecifier
    : IDENTIFIER (AS IDENTIFIER)?
    ;

deferStatement
    : DEFER blockStatement #deferBlockStmt
    ;

// --- 外部符号声明 (declare) ---
/**
 * declare 用于声明「存在但实现在别处」的符号 —— 典型是 C 绑定的外部库
 * (如 SDL)、或运行时由宿主注册的全局。它在编译期被完全擦除 (不产生任何
 * 字节码、不创建任何绑定)，仅供类型检查与 LSP 消费 (跳转/hover/补全)。
 * 描述通过前置文档注释 (/// 或 /**) 提供。
 *
 * 设计与 SPT「类型/const 是提示、运行期无效」的既有哲学一致。
 */

/**
 * 模块声明块：描述一个外部模块的导出形状。
 * 复用 import 的 `from "..."`，与 `import ... from "..."` 的绑定关系一一对应：
 *   declare from "sdl" { int Init(int flags); ... }
 */
declareModule
    : DECLARE FROM STRING_LITERAL OCB declarationMember* CCB #declareModuleDef
    ;

/** 环境声明：顶层单个外部符号 (宿主注册为全局时使用)。 */
ambientDeclaration
    : DECLARE declarationMember #ambientDeclarationDef
    ;

/**
 * 声明成员 = 「签名」。与普通声明同形，但：
 *   - 不允许 auto (声明必须给出确切类型，无初始化器可推断)；
 *   - 不允许初始化器；
 *   - 函数/方法体替换为 ';'。
 */
declarationMember
    : GLOBAL? CONST? type IDENTIFIER SEMICOLON                        #declVarMember
    | CONST? type qualifiedIdentifier OP parameterList? CP SEMICOLON  #declFuncMember
    | CONST? VARS  qualifiedIdentifier OP parameterList? CP SEMICOLON #declMultiFuncMember
    | CONST? FUNCTION qualifiedIdentifier OP parameterList? CP (ARROW returnType)? SEMICOLON #declFnFuncMember
    | CLASS IDENTIFIER OCB declClassMember* CCB                       #declClassMember
    | SEMICOLON                                                       #declEmptyMember
    ;

/** 声明里的类成员：字段签名 + 方法签名 (无体、无初始化器、无 auto)。 */
declClassMember
    : STATIC? CONST? type IDENTIFIER SEMICOLON                          #declFieldMember
    | STATIC? CONST? type IDENTIFIER OP parameterList? CP SEMICOLON     #declMethodMember
    | STATIC? CONST? VARS IDENTIFIER OP parameterList? CP SEMICOLON     #declMultiMethodMember
    | STATIC? CONST? FUNCTION IDENTIFIER OP parameterList? CP (ARROW returnType)? SEMICOLON #declFnMethodMember
    | SEMICOLON                                                         #declClassEmptyMember
    ;

// --- 赋值语句 ---
/** 更新赋值语句: lvalue op= expression */
updateStatement
 : lvalue op=(ADD_ASSIGN | SUB_ASSIGN | MUL_ASSIGN | DIV_ASSIGN | IDIV_ASSIGN | MOD_ASSIGN | CONCAT_ASSIGN) expression  #updateAssignStmt
 ;

/** 赋值语句: lvalue, lvalue = expression, expression */
assignStatement
 : lvalue (COMMA lvalue)* ASSIGN expression (COMMA expression)* #normalAssignStmt
 ;

// --- 左值规则 (L-Value) ---
/** 定义可以出现在赋值左侧的表达式 (左值) */
lvalue
 : (IDENTIFIER) ( lvalueSuffix )* #lvalueBase // 以标识符开始，可选后缀
 ;

/** 左值允许的后缀 (索引或成员访问) */
lvalueSuffix
 : OSB expression CSB      #lvalueIndex   // 数组/Map 索引 [exp]
 | DOT IDENTIFIER #lvalueMember   // 成员访问 .ident
 ;

// --- 声明 ---
/** 声明的种类 */
declaration
    :
    EXPORT? (variableDeclaration SEMICOLON  // 变量声明
    | functionDeclaration // 函数声明
    | classDeclaration// 类声明
    )
    ;

/**
 * 变量声明 (强制显式类型或 auto):
 * 每个变量都需要自己的类型注解。
 * 支持可选的初始化赋值 。
 * 形式一/二为单变量；形式三为带类型的多变量声明 (int a, str b = expr;)
 */
variableDeclaration
    :
    //语言支持多个声明
    GLOBAL? CONST? declaration_item (ASSIGN expression)? #variableDeclarationDef
    | GLOBAL? CONST? declaration_item (COMMA declaration_item)+ (ASSIGN expression)? #typedMultiVariableDeclarationDef
    | VARS GLOBAL? CONST? IDENTIFIER (COMMA GLOBAL? CONST? IDENTIFIER)* (ASSIGN expression)? #mutiVariableDeclarationDef
    ;

/** 辅助规则: 单个声明项 (类型/auto + 标识符) */
declaration_item
    : (type | AUTO) IDENTIFIER
    ;

/** 函数声明/定义 (支持 global/const 和 qualifiedIdentifier)
 *  形式一: type name(params) { } —— 单一返回类型
 *  形式二: vars name(params) { } —— 多返回值 (旧语法，类型未知)
 *  形式三: fn name(params) (-> retType)? { } —— 新语法，可选多返回类型注解 */
functionDeclaration
     : GLOBAL? CONST? type qualifiedIdentifier OP parameterList? CP blockStatement #functionDeclarationDef
     | GLOBAL? CONST? VARS qualifiedIdentifier OP parameterList? CP blockStatement #multiReturnFunctionDeclarationDef
     | GLOBAL? CONST? FUNCTION qualifiedIdentifier OP parameterList? CP (ARROW returnType)? blockStatement #fnFunctionDeclarationDef
     ;

/** 类声明/定义 */
classDeclaration
    : CLASS IDENTIFIER OCB classMember* CCB #classDeclarationDef
    ;

/** 类成员 (字段或方法) */
classMember
    // 静态或实例字段声明 (类型或 auto 必须)
    : STATIC? CONST? declaration_item (ASSIGN expression)? #classFieldMember
    // 静态或实例方法声明
    | STATIC? CONST? type IDENTIFIER OP parameterList? CP blockStatement #classMethodMember
    | STATIC? CONST? VARS IDENTIFIER OP parameterList? CP blockStatement #multiReturnClassMethodMember
    // fn 方法 (新语法): fn name(params) (-> retType)? { }
    | STATIC? CONST? FUNCTION IDENTIFIER OP parameterList? CP (ARROW returnType)? blockStatement #fnClassMethodMember
    // 空成员 (允许只有分号)
    | SEMICOLON #classEmptyMember
    ;



// --- 类型注解 ---
/** 类型注解 */
type
    : primitiveType             #typePrimitive // 基本类型
    | listType                  #typeListType  // List 类型 (泛型可选)
    | mapType                   #typeMap       // Map 类型 (泛型可选)
    | ANY                       #typeAny       // any
    | qualifiedIdentifier       #typeQualifiedIdentifier
    ;

qualifiedIdentifier
    : IDENTIFIER (DOT IDENTIFIER)*
    ;

/** 基本类型 */
primitiveType
    : INT | FLOAT | NUMBER | STR | BOOL | VOID | NULL_ | COROUTINE | FUNCTION
    ;

/** List 类型注解: list 或 list<Type> */
listType
    : LIST (LT type GT)?
    ;

/** Map 类型注解: map 或 map<KeyType, ValueType> */
mapType
    : MAP (LT type COMMA type GT)?
    ;

// --- 表达式 (按优先级从低到高排列) ---

/** 表达式入口 (逻辑或 ||, 不再包含赋值) */
expression
    : logicalOrExp
    ;

/** 表达式列表 (用于函数调用参数, list/map 字面量等) */
expressionList
    : expression (COMMA expression)*
    ;

// --- 赋值表达式规则已移除, 赋值作为语句处理 ---

/** 逻辑或 (||) - 左结合 */
logicalOrExp
    : logicalAndExp (OR logicalAndExp)* #logicalOrExpression
    ;

/** 逻辑与 (&&) - 左结合 */
logicalAndExp
    : bitwiseOrExp (AND bitwiseOrExp)* #logicalAndExpression
    ;

/** 按位或 (|) - 左结合 */
bitwiseOrExp
    : bitwiseXorExp (BIT_OR bitwiseXorExp)* #bitwiseOrExpression
    ;

/** 按位异或 (^) - 左结合 */
bitwiseXorExp
    : bitwiseAndExp (BIT_XOR bitwiseAndExp)* #bitwiseXorExpression
    ;

/** 按位与 (&) - 左结合 */
bitwiseAndExp
    : equalityExp (BIT_AND equalityExp)* #bitwiseAndExpression
    ;

/** 相等性比较 (==, !=) - 左结合 */
equalityExp
    : comparisonExp (equalityExpOp comparisonExp)* #equalityExpression
    ;
equalityExpOp:(EQ | NEQ);

/** 大小比较 (<, >, <=, >=) - 左结合 */
comparisonExp
    : shiftExp (comparisonExpOp shiftExp)* #comparisonExpression
    ;
comparisonExpOp:(LT | GT | LTE | GTE);

/** 位移 (<<, >>) - 左结合 */
shiftExp
    : concatExp (shiftExpOp concatExp)* #shiftExpression
    ;
shiftExpOp:(LSHIFT | GT GT);

/** 字符串连接 (..) - 右结合（与 Lua 一致；codegen codeconcat 依赖右结合合并 CONCAT 指令） */
concatExp
    : addSubExp (CONCAT concatExp)? #concatExpression
    ;

/** 加减法 (+, -) - 左结合 */
addSubExp
    : mulDivModExp (addSubExpOp mulDivModExp)* #addSubExpression
    ;
addSubExpOp:(ADD | SUB);

/** 乘除模 (*, /, %) - 左结合 */
mulDivModExp
    : unaryExp (mulDivModExpOp unaryExp)* #mulDivModExpression
    ;
mulDivModExpOp:(MUL | DIV | IDIV | MOD);
/** 一元前缀运算符 (!, -, #, ~) - 右结合 */
unaryExp
    : (NOT | SUB | LEN | BIT_NOT) unaryExp #unaryPrefix
    | postfixExp                           #unaryToPostfix
    ;

/** 后缀表达式 (函数调用, 索引, 成员访问) - 最高优先级 */
postfixExp
    : primaryExp postfixSuffix* #postfixExpression
    ;

/** 后缀操作符 */
postfixSuffix
    : OSB expression CSB             #postfixIndexSuffix      // 索引: expr[index]
    | DOT IDENTIFIER                 #postfixMemberSuffix     // 成员访问: expr.member
    | OP arguments? CP               #postfixCallSuffix       // 函数调用: expr(args)
    ;

/** 主要/原子表达式 (构成后缀表达式的基础) */
primaryExp
    : atomexp              #primaryAtom         // 原子字面量
    | listExpression       #primaryListLiteral  // List 字面量
    | mapExpression        #primaryMapLiteral   // Map 字面量
    | IDENTIFIER           #primaryIdentifier   // 标识符
    | DDD                  #primaryVarArgs      // 可变参数 '...' (在函数体内使用时)
    | OP expression CP     #primaryParenExp     // 圆括号表达式
    | lambdaExpression     #primaryLambda       // Lambda/匿名函数
    ;

/** 原子字面量 */
atomexp
    : NULL_ | TRUE | FALSE | INTEGER | FLOAT_LITERAL | STRING_LITERAL
    ;

/** Lambda/匿名函数表达式: function (parameterList?) (-> returnType)? { body }
 * 返回类型注解可选：省略时由上下文/LSP 推断，codegen 不依赖该字段。
 * 支持多返回类型: fn(int x) -> int, str { } */
lambdaExpression
	: FUNCTION OP parameterList? CP (ARROW returnType)? blockStatement #lambdaExprDef
	;

/** 返回类型注解: 单类型 | 多类型 (逗号分隔) | VARS (旧语法，类型未知) */
returnType
    : VARS                                #returnTypeVars
    | type (COMMA type)*                  #returnTypeTyped
    ;

/** List 字面量: [elem1, elem2, ...] */
listExpression
    : OSB expressionList? CSB #listLiteralDef
    ;

/** Map 字面量: {key1: val1, key2: val2, ...} */
mapExpression
    : OCB mapEntryList? CCB #mapLiteralDef
    ;

/** Map 字面量中的条目列表 */
mapEntryList
    : mapEntry (COMMA mapEntry)*
    ;

/** Map 字面量中的单个条目 */
mapEntry
    : IDENTIFIER COL expression      #mapEntryIdentKey     // key: value
    | OSB expression CSB COL expression #mapEntryExprKey      // [expr]: value
    | STRING_LITERAL COL expression  #mapEntryStringKey    // "key": value
    | INTEGER COL expression         #mapEntryIntKey       // 1: value
    | FLOAT_LITERAL COL expression   #mapEntryFloatKey     // 1.5: value
    ;

// --- 控制流语句 ---
/**
 * If 语句
 * body 可以是 blockStatement（花括号）或单个 statement（无花括号）。
 * dangling-else 由 ANTLR 的最长匹配默认解决（else 绑定最近的 if）。
 */
ifStatement
    : IF OP expression CP body
      (ELSE IF OP expression CP body)*
      (ELSE body)?
    ;

/** While 语句 */
whileStatement
    : WHILE OP expression CP body
    ;

// --- For 循环 ---
/**
 * For 语句 (数值 for + 泛型 for-each)
 * 去掉了 C 风格 for(init;cond;update)，改为 Lua 风格数值 for
 */
forStatement
    : FOR OP forControl CP body
    ;

/** 循环/分支体：花括号代码块或单条语句 */
body
    : blockStatement
    | statement
    ;

/**
 * For 循环的控制部分
 *
 * 注意：forEachControl 的 expressionList 在运行时只取前 4 个值
 * (receiver + iteratorFunc + state + closeVar)，多余的会被静默丢弃。
 * 这是为了对齐 SPT 的 slot-0 receiver 调用约定 + Lua 5.5 的 forlist 协议。
 * 通常用户只写 1 个表达式（如 pairs(t)），由运行时展开为 3~4 个值。
 */
forControl
    // 形式一: 数值 for — 直接映射到 Lua 的 for i = start, end [, step]
    //   for ([type|auto] i = start, end)          { ... }
    //   for ([type|auto] i = start, end, step)    { ... }
    : forNumericVar ASSIGN expression COMMA expression (COMMA expression)? #forNumericControl
    // 形式二: 泛型 for-each — 映射到 Lua 的 for k, v in iterFunc, state, initVal
    //   for ([type|auto] k, [type|auto] v : pairs(t))
    //   for (k, v : ipairs(t))
    | forEachVar (COMMA forEachVar)* COL expressionList #forEachControl
    ;

/**
 * 数值 for 的循环变量 (可选类型注解)
 *   int i / auto i / i
 */
forNumericVar
    : (type | AUTO) IDENTIFIER  #forNumericVarTyped   // 带类型: int i, auto i
    | IDENTIFIER                #forNumericVarUntyped // 无类型: i (Lua 风格)
    ;

/**
 * 泛型 for-each 的循环变量 (可选类型注解)
 *   string k / auto k / k
 */
forEachVar
    : (type | AUTO) IDENTIFIER  #forEachVarTyped   // 带类型: string k
    | IDENTIFIER                #forEachVarUntyped // 无类型: k
    ;


// --- 函数参数与调用 ---
/** 函数定义中的参数列表 */
parameterList
    : parameter (COMMA parameter)* (COMMA DDD)? // 参数
    | DDD
    ;

/** 函数定义中的单个参数 (必须带类型) */
parameter
    : type IDENTIFIER
    ;

/** 函数调用时的参数列表 (零个或多个表达式) */
arguments
    : expressionList?
    ;
