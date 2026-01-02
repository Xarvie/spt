
// Generated from LangLexer.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"




class  LangLexer : public antlr4::Lexer {
public:
  enum {
    INT = 1, FLOAT = 2, NUMBER = 3, STRING = 4, BOOL = 5, ANY = 6, VOID = 7, 
    NULL_ = 8, LIST = 9, MAP = 10, FUNCTION = 11, COROUTINE = 12, MUTIVAR = 13, 
    IF = 14, ELSE = 15, WHILE = 16, FOR = 17, BREAK = 18, CONTINUE = 19, 
    RETURN = 20, DEFER = 21, TRUE = 22, FALSE = 23, CONST = 24, AUTO = 25, 
    GLOBAL = 26, STATIC = 27, IMPORT = 28, AS = 29, TYPE = 30, FROM = 31, 
    PRIVATE = 32, EXPORT = 33, CLASS = 34, NEW = 35, ADD = 36, SUB = 37, 
    MUL = 38, DIV = 39, MOD = 40, ASSIGN = 41, ADD_ASSIGN = 42, SUB_ASSIGN = 43, 
    MUL_ASSIGN = 44, DIV_ASSIGN = 45, MOD_ASSIGN = 46, CONCAT_ASSIGN = 47, 
    EQ = 48, NEQ = 49, LT = 50, GT = 51, LTE = 52, GTE = 53, AND = 54, OR = 55, 
    NOT = 56, CONCAT = 57, LEN = 58, BIT_AND = 59, BIT_OR = 60, BIT_XOR = 61, 
    BIT_NOT = 62, LSHIFT = 63, ARROW = 64, OP = 65, CP = 66, OSB = 67, CSB = 68, 
    OCB = 69, CCB = 70, COMMA = 71, DOT = 72, COL = 73, SEMICOLON = 74, 
    DDD = 75, INTEGER = 76, FLOAT_LITERAL = 77, STRING_LITERAL = 78, IDENTIFIER = 79, 
    WS = 80, LINE_COMMENT = 81, BLOCK_COMMENT = 82
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

