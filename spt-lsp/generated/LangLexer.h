
// Generated from LangLexer.g4 by ANTLR 4.13.1

#pragma once

#include "antlr4-runtime.h"

class LangLexer : public antlr4::Lexer {
public:
  enum {
    INT = 1,
    FLOAT = 2,
    NUMBER = 3,
    STRING = 4,
    BOOL = 5,
    ANY = 6,
    VOID = 7,
    NULL_ = 8,
    LIST = 9,
    MAP = 10,
    FUNCTION = 11,
    COROUTINE = 12,
    VARS = 13,
    IF = 14,
    ELSE = 15,
    WHILE = 16,
    FOR = 17,
    BREAK = 18,
    CONTINUE = 19,
    RETURN = 20,
    DEFER = 21,
    TRUE = 22,
    FALSE = 23,
    CONST = 24,
    AUTO = 25,
    GLOBAL = 26,
    STATIC = 27,
    IMPORT = 28,
    AS = 29,
    FROM = 30,
    PRIVATE = 31,
    EXPORT = 32,
    CLASS = 33,
    NEW = 34,
    ADD = 35,
    SUB = 36,
    MUL = 37,
    DIV = 38,
    IDIV = 39,
    MOD = 40,
    ASSIGN = 41,
    ADD_ASSIGN = 42,
    SUB_ASSIGN = 43,
    MUL_ASSIGN = 44,
    DIV_ASSIGN = 45,
    IDIV_ASSIGN = 46,
    MOD_ASSIGN = 47,
    CONCAT_ASSIGN = 48,
    EQ = 49,
    NEQ = 50,
    LT = 51,
    GT = 52,
    LTE = 53,
    GTE = 54,
    AND = 55,
    OR = 56,
    NOT = 57,
    CONCAT = 58,
    LEN = 59,
    BIT_AND = 60,
    BIT_OR = 61,
    BIT_XOR = 62,
    BIT_NOT = 63,
    LSHIFT = 64,
    ARROW = 65,
    OP = 66,
    CP = 67,
    OSB = 68,
    CSB = 69,
    OCB = 70,
    CCB = 71,
    COMMA = 72,
    DOT = 73,
    COL = 74,
    SEMICOLON = 75,
    DDD = 76,
    INTEGER = 77,
    FLOAT_LITERAL = 78,
    STRING_LITERAL = 79,
    IDENTIFIER = 80,
    WS = 81,
    LINE_COMMENT = 82,
    BLOCK_COMMENT = 83
  };

  explicit LangLexer(antlr4::CharStream *input);

  ~LangLexer() override;

  std::string getGrammarFileName() const override;

  const std::vector<std::string> &getRuleNames() const override;

  const std::vector<std::string> &getChannelNames() const override;

  const std::vector<std::string> &getModeNames() const override;

  const antlr4::dfa::Vocabulary &getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN &getATN() const override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.
};
