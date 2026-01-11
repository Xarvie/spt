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
    ;

//-- 导入语句 --
importStatement
    : IMPORT MUL AS IDENTIFIER FROM STRING_LITERAL               #importNamespaceStmt
    | IMPORT OCB importSpecifier (COMMA importSpecifier)* CCB FROM STRING_LITERAL #importNamedStmt
    ;

importSpecifier
    : TYPE? IDENTIFIER (AS IDENTIFIER)?
    ;

deferStatement
    : DEFER blockStatement #deferBlockStmt
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
 */
variableDeclaration
    :
    //语言支持多个声明
    GLOBAL? CONST? declaration_item (ASSIGN expression)? #variableDeclarationDef
    | MUTIVAR GLOBAL? CONST? IDENTIFIER (COMMA GLOBAL? CONST? IDENTIFIER)* (ASSIGN expression)? #mutiVariableDeclarationDef
    ;

/** 辅助规则: 单个声明项 (类型/auto + 标识符) */
declaration_item
    : (type | AUTO) IDENTIFIER
    ;

/** 函数声明/定义 (支持 global 和 qualifiedIdentifier, 仅支持单一返回类型或 void) */
functionDeclaration
     : GLOBAL? type qualifiedIdentifier OP parameterList? CP blockStatement #functionDeclarationDef
     | GLOBAL? MUTIVAR qualifiedIdentifier OP parameterList? CP blockStatement #multiReturnFunctionDeclarationDef
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
    | STATIC? type IDENTIFIER OP parameterList? CP blockStatement #classMethodMember
    | STATIC? MUTIVAR IDENTIFIER OP parameterList? CP blockStatement #multiReturnClassMethodMember // <<< 使用 MUTIVAR 关键字
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
    : INT | FLOAT | NUMBER | STRING | BOOL | VOID | NULL_ | COROUTINE | FUNCTION
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

/** 字符串连接 (..) - 左结合 (注意: 若需右结合, 语法需调整) */
concatExp
    : addSubExp (CONCAT addSubExp)* #concatExpression
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
    | COL IDENTIFIER                 #postfixColonLookupSuffix// 成员查找: expr:member
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
    | newExp               #primaryNew          // new 表达式
    | lambdaExpression     #primaryLambda       // Lambda/匿名函数
    ;

/** 原子字面量 */
atomexp
    : NULL_ | TRUE | FALSE | INTEGER | FLOAT_LITERAL | STRING_LITERAL
    ;

/** Lambda/匿名函数表达式: function (parameterList?) -> type { body } */
lambdaExpression
 : FUNCTION OP parameterList? CP ARROW (type | MUTIVAR) blockStatement #lambdaExprDef
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
    ;

/** 'new' 表达式: new ClassName(args?) - 强制带括号 */
newExp
    : NEW qualifiedIdentifier (OP arguments? CP) #newExpressionDef
    ;

// --- 控制流语句 ---
/** If 语句 */
ifStatement
    : IF OP expression CP blockStatement // if 块必须用 {} 包裹
      (ELSE IF OP expression CP blockStatement)* // 零或多个 else if
      (ELSE blockStatement)? // 可选的 else
    ;

/** While 语句 */
whileStatement
    : WHILE OP expression CP blockStatement // while 块必须用 {} 包裹
    ;

// --- For 循环 ---
/** For 语句 (包含 C 风格和 For-Each 风格) */
forStatement
    : FOR OP forControl CP blockStatement // for 块必须用 {} 包裹
    ;

/** For 循环的控制部分 */
forControl
    // 形式一: C 风格 (init; condition; update)
    : forInitStatement SEMICOLON expression? SEMICOLON forUpdate? #forCStyleControl
    // 注意: update 部分允许零或多个逗号分隔的更新表达式/语句
    // 形式二: For-Each 风格 (Type var1, Type var2 : collection)
    | declaration_item (COMMA declaration_item)* COL expression #forEachExplicitControl
    ;

forUpdate:
forUpdateSingle (COMMA forUpdateSingle)*
;

forUpdateSingle:
expression | updateStatement | assignStatement
;

forInitStatement // 代表 C 的 init-statement
    : multiDeclaration // 允许多个 'Type ID = val'
    | assignStatement//赋值
    | expressionList?            // 允许表达式列表 (如 i=0, j=0) 或空
    | // 允许完全为空
    ;

multiDeclaration
    : declaration_item (ASSIGN expression)? (COMMA declaration_item (ASSIGN expression)?)*
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

