/*
** spt_token.h
** ---------------------------------------------------------------------------
** Token 种类与 Token 结构。Token 种类直接对应 LangLexer.g4 中的词法单元，
** 命名沿用语法文件以便对照（见迁移规划书 附录 A）。
**
** 注意：语法层故意不产生 `>>`(RSHIFT) —— 右移由解析器用两个 TOK_GT 合成，
** 以消解 `list<list<int>>` 的歧义（见 LangLexer.g4 注释 / 规划书 §5.1）。
** ---------------------------------------------------------------------------
*/
#ifndef SPT_TOKEN_H
#define SPT_TOKEN_H

typedef enum {
  TOK_EOF = 0,

  /* ---- 类型关键字 ---- */
  TOK_INT,       /* int      */
  TOK_FLOAT,     /* float    */
  TOK_NUMBER,    /* number   */
  TOK_STR,       /* str      */
  TOK_BOOL,      /* bool     */
  TOK_ANY,       /* any      */
  TOK_VOID,      /* void     */
  TOK_NULL,      /* null     */
  TOK_LIST,      /* list     */
  TOK_MAP,       /* map      */
  TOK_FUNCTION,  /* function */
  TOK_COROUTINE, /* coro     */
  TOK_VARS,      /* vars     */

  /* ---- 控制流关键字 ---- */
  TOK_IF,
  TOK_ELSE,
  TOK_WHILE,
  TOK_FOR,
  TOK_BREAK,
  TOK_CONTINUE,
  TOK_RETURN,
  TOK_DEFER,

  /* ---- 字面/常量关键字 ---- */
  TOK_TRUE,
  TOK_FALSE,
  TOK_CONST,

  /* ---- 声明/作用域关键字 ---- */
  TOK_AUTO,
  TOK_GLOBAL,
  TOK_STATIC,
  TOK_IMPORT,
  TOK_AS,
  TOK_FROM,
  TOK_EXPORT,
  TOK_DECLARE, /* declare */

  /* ---- 面向对象 ---- */
  TOK_CLASS,

  /* ---- 运算符 ---- */
  TOK_ADD,           /* +    */
  TOK_SUB,           /* -    */
  TOK_MUL,           /* *    */
  TOK_DIV,           /* /    */
  TOK_IDIV,          /* ~/   */
  TOK_MOD,           /* %    */
  TOK_ASSIGN,        /* =    */
  TOK_ADD_ASSIGN,    /* +=   */
  TOK_SUB_ASSIGN,    /* -=   */
  TOK_MUL_ASSIGN,    /* *=   */
  TOK_DIV_ASSIGN,    /* /=   */
  TOK_IDIV_ASSIGN,   /* ~/=  */
  TOK_MOD_ASSIGN,    /* %=   */
  TOK_CONCAT_ASSIGN, /* ..=  */
  TOK_EQ,            /* ==   */
  TOK_NEQ,           /* !=   */
  TOK_LT,            /* <    */
  TOK_GT,            /* >    */
  TOK_LTE,           /* <=   */
  TOK_GTE,           /* >=   */
  TOK_AND,           /* &&   */
  TOK_OR,            /* ||   */
  TOK_NOT,           /* !    */
  TOK_CONCAT,        /* ..   */
  TOK_LEN,           /* #    */
  TOK_BIT_AND,       /* &    */
  TOK_BIT_OR,        /* |    */
  TOK_BIT_XOR,       /* ^    */
  TOK_BIT_NOT,       /* ~    */
  TOK_LSHIFT,        /* <<   */
  TOK_ARROW,         /* ->   */

  /* ---- 分隔符 ---- */
  TOK_LPAREN,    /* (   */
  TOK_RPAREN,    /* )   */
  TOK_LBRACKET,  /* [   */
  TOK_RBRACKET,  /* ]   */
  TOK_LBRACE,    /* {   */
  TOK_RBRACE,    /* }   */
  TOK_COMMA,     /* ,   */
  TOK_DOT,       /* .   */
  TOK_COLON,     /* :   */
  TOK_SEMICOLON, /* ;   */
  TOK_ELLIPSIS,  /* ... */

  /* ---- 字面量 ---- */
  TOK_INTEGER,        /* 123 / 0xFF        */
  TOK_FLOAT_LITERAL,  /* 1.5 / 1e3 / 0x1p4 */
  TOK_STRING_LITERAL, /* "..." / '...'     */
  TOK_IDENTIFIER,     /* 含 Unicode        */

  TOK_KIND_COUNT
} SptTokenKind;

typedef struct {
  SptTokenKind kind;
  const char *lexeme; /* 指向源码缓冲（非以 NUL 结尾），长度由 length 给出 */
  int length;         /* 词素字节数 */
  int line;           /* 起始行（1 起） */
  int column;         /* 起始列（1 起，按字节计） */
  const char *doc;    /* 紧邻其前的文档注释（行文档或块文档），NUL 结尾；无则 NULL */
} SptToken;

/* 返回 token 种类的可读名（用于错误信息），如 TOK_RPAREN -> "')'"。 */
const char *spt_token_name(SptTokenKind kind);

#endif /* SPT_TOKEN_H */
