
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
    TYPE = 30,
    FROM = 31,
    PRIVATE = 32,
    EXPORT = 33,
    CLASS = 34,
    NEW = 35,
    ADD = 36,
    SUB = 37,
    MUL = 38,
    DIV = 39,
    IDIV = 40,
    MOD = 41,
    ASSIGN = 42,
    ADD_ASSIGN = 43,
    SUB_ASSIGN = 44,
    MUL_ASSIGN = 45,
    DIV_ASSIGN = 46,
    IDIV_ASSIGN = 47,
    MOD_ASSIGN = 48,
    CONCAT_ASSIGN = 49,
    EQ = 50,
    NEQ = 51,
    LT = 52,
    GT = 53,
    LTE = 54,
    GTE = 55,
    AND = 56,
    OR = 57,
    NOT = 58,
    CONCAT = 59,
    LEN = 60,
    BIT_AND = 61,
    BIT_OR = 62,
    BIT_XOR = 63,
    BIT_NOT = 64,
    LSHIFT = 65,
    ARROW = 66,
    OP = 67,
    CP = 68,
    OSB = 69,
    CSB = 70,
    OCB = 71,
    CCB = 72,
    COMMA = 73,
    DOT = 74,
    COL = 75,
    SEMICOLON = 76,
    DDD = 77,
    INTEGER = 78,
    FLOAT_LITERAL = 79,
    STRING_LITERAL = 80,
    IDENTIFIER = 81,
    WS = 82,
    LINE_COMMENT = 83,
    BLOCK_COMMENT = 84
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
