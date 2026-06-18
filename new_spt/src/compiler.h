/*
 * compiler.h — Internal interface shared by the lexer, parser, and codegen.
 *
 * This is the SPT frontend: source text -> tokens -> AST -> bytecode Proto.
 * It implements a focused but real subset of the language:
 *   - default-local variables, with `global NAME = e;` for globals
 *   - arithmetic (+ - * / %), comparison (== != < <= > >=), unary - and !
 *   - if/else, while, return, blocks
 *   - function declarations (bound as globals so they are callable anywhere,
 *     including recursively) and calls, honouring the Slot-0 convention
 *   - List literals [a, b, c] and indexing a[i] (read and write)
 *
 * Not yet handled (see README roadmap): the static type system and the
 * compile-time-only type/const/declare erasure, closures that capture outer
 * locals (references to non-locals resolve to globals for now), Map literals,
 * and && / || short-circuit operators.
 */
#ifndef SPT_COMPILER_H
#define SPT_COMPILER_H

#include "spt/state.h"

/* ---- static types (compile-time only; the runtime stays tagged) ---- */
typedef enum {
  TY_DYN,        /* unknown / dynamic — no static guarantees             */
  TY_INT, TY_FLOAT, TY_STRING, TY_BOOL, TY_NULL, TY_LIST, TY_MAP
} Type;

/* ---- tokens ---- */
typedef enum {
  TK_EOF, TK_INT, TK_FLOAT, TK_STR, TK_NAME,
  TK_GLOBAL, TK_FUNCTION, TK_IF, TK_ELSE, TK_WHILE, TK_RETURN,
  TK_TRUE, TK_FALSE, TK_NULL,
  TK_KW_INT, TK_KW_FLOAT, TK_KW_STRING, TK_KW_BOOL, TK_CONST,   /* type system */
  TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
  TK_EQ, TK_NE, TK_LT, TK_LE, TK_GT, TK_GE, TK_ASSIGN, TK_NOT,
  TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_LBRACKET, TK_RBRACKET,
  TK_COMMA, TK_SEMI, TK_ARROW
} TokenType;

typedef struct {
  TokenType   type;
  const char *s;     /* for TK_NAME / TK_STR: pointer into source           */
  int         len;
  int64_t     ival;
  double      fval;
  int         line;
} Token;

typedef struct {
  const char *p;
  int         line;
} Lexer;

void lex_init(Lexer *lx, const char *src);
void lex_next(Lexer *lx, Token *t);

/* ---- AST ---- */
typedef enum {
  N_INT, N_FLOAT, N_STR, N_BOOL, N_NULL, N_NAME,
  N_BINOP, N_UNOP, N_CALL, N_INDEX, N_LIST,
  N_BLOCK, N_IF, N_WHILE, N_RETURN, N_ASSIGN, N_GLOBAL, N_FUNC, N_EXPRSTMT,
  N_VARDECL
} NodeKind;

typedef struct Node Node;
struct Node {
  NodeKind kind;
  int      line;
  union {
    int64_t ival;
    double  fval;
    int     bval;
    struct { const char *s; int len; } str;             /* N_STR, N_NAME      */
    struct { TokenType op; Node *l, *r; } bin;           /* N_BINOP, N_UNOP    */
    struct { Node *fn; Node **args; int nargs; } call;   /* N_CALL             */
    struct { Node *obj, *idx; } index;                   /* N_INDEX            */
    struct { Node **elems; int n; } list;                /* N_LIST, N_BLOCK    */
    struct { Node *cond, *a, *b; } ctrl;                 /* N_IF/N_WHILE       */
    struct { Node *e; } one;                             /* N_RETURN/N_EXPRSTMT*/
    struct { Node *target, *value; } assign;             /* N_ASSIGN           */
    struct { const char *s; int len; Node *value; } glob;/* N_GLOBAL           */
    struct { const char *s; int len; Type ty; int is_const; Node *value; } vardecl; /* N_VARDECL */
    struct { const char *s; int len; Node **params; Type *ptypes; int nparams;
             Type ret; Node *body; } func;               /* N_FUNC             */
  } u;
};

/* ---- AST arena (bump allocator, freed wholesale after codegen) ---- */
typedef struct Arena Arena;
typedef struct { Arena *head; } AstArena;
void *arena_alloc(AstArena *a, size_t n);
void  arena_free(AstArena *a);

/* ---- frontend stages ---- */
/* Parse `src` into a chunk (N_BLOCK). Returns NULL on syntax error, writing a
 * message into `err`. */
Node *spt_parse(spt_State *L, AstArena *arena, const char *src, char *err, size_t errsz);

/* Compile a chunk into a top-level Proto. Returns NULL on error (message in
 * `err`). GC must be disabled across this call by the caller. */
Proto *spt_codegen(spt_State *L, Node *chunk, const char *chunkname, char *err, size_t errsz);

#endif /* SPT_COMPILER_H */
