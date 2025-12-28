
// Generated from LangLexer.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"




class  LangLexer : public antlr4::Lexer {
public:
  enum {
    INT = 1, FLOAT = 2, NUMBER = 3, STRING = 4, BOOL = 5, ANY = 6, VOID = 7, 
    NULL_ = 8, LIST = 9, MAP = 10, FUNCTION = 11, COROUTINE = 12, MUTIVAR = 13, 
    IF = 14, ELSE = 15, WHILE = 16, FOR = 17, BREAK = 18, CONTINUE = 19, 
    RETURN = 20, TRUE = 21, FALSE = 22, CONST = 23, AUTO = 24, GLOBAL = 25, 
    STATIC = 26, IMPORT = 27, AS = 28, TYPE = 29, FROM = 30, PRIVATE = 31, 
    EXPORT = 32, CLASS = 33, NEW = 34, ADD = 35, SUB = 36, MUL = 37, DIV = 38, 
    MOD = 39, ASSIGN = 40, ADD_ASSIGN = 41, SUB_ASSIGN = 42, MUL_ASSIGN = 43, 
    DIV_ASSIGN = 44, MOD_ASSIGN = 45, CONCAT_ASSIGN = 46, EQ = 47, NEQ = 48, 
    LT = 49, GT = 50, LTE = 51, GTE = 52, AND = 53, OR = 54, NOT = 55, CONCAT = 56, 
    LEN = 57, BIT_AND = 58, BIT_OR = 59, BIT_XOR = 60, BIT_NOT = 61, LSHIFT = 62, 
    ARROW = 63, OP = 64, CP = 65, OSB = 66, CSB = 67, OCB = 68, CCB = 69, 
    COMMA = 70, DOT = 71, COL = 72, SEMICOLON = 73, DDD = 74, INTEGER = 75, 
    FLOAT_LITERAL = 76, STRING_LITERAL = 77, IDENTIFIER = 78, WS = 79, LINE_COMMENT = 80, 
    BLOCK_COMMENT = 81
  };

  explicit LangLexer(antlr4::CharStream *input);

  ~LangLexer() override;


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.

};

