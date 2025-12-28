
// Generated from LangParser.g4 by ANTLR 4.13.1


#include "LangParserVisitor.h"

#include "LangParser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct LangParserStaticData final {
  LangParserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  LangParserStaticData(const LangParserStaticData&) = delete;
  LangParserStaticData(LangParserStaticData&&) = delete;
  LangParserStaticData& operator=(const LangParserStaticData&) = delete;
  LangParserStaticData& operator=(LangParserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag langparserParserOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
LangParserStaticData *langparserParserStaticData = nullptr;

void langparserParserInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (langparserParserStaticData != nullptr) {
    return;
  }
#else
  assert(langparserParserStaticData == nullptr);
#endif
  auto staticData = std::make_unique<LangParserStaticData>(
    std::vector<std::string>{
      "compilationUnit", "blockStatement", "statement", "importStatement", 
      "importSpecifier", "updateStatement", "assignStatement", "lvalue", 
      "lvalueSuffix", "declaration", "variableDeclaration", "declaration_item", 
      "functionDeclaration", "classDeclaration", "classMember", "type", 
      "qualifiedIdentifier", "primitiveType", "listType", "mapType", "expression", 
      "expressionList", "logicalOrExp", "logicalAndExp", "bitwiseOrExp", 
      "bitwiseXorExp", "bitwiseAndExp", "equalityExp", "equalityExpOp", 
      "comparisonExp", "comparisonExpOp", "shiftExp", "shiftExpOp", "concatExp", 
      "addSubExp", "addSubExpOp", "mulDivModExp", "mulDivModExpOp", "unaryExp", 
      "postfixExp", "postfixSuffix", "primaryExp", "atomexp", "lambdaExpression", 
      "listExpression", "mapExpression", "mapEntryList", "mapEntry", "newExp", 
      "ifStatement", "whileStatement", "forStatement", "forControl", "forUpdate", 
      "forUpdateSingle", "forInitStatement", "multiDeclaration", "parameterList", 
      "parameter", "arguments"
    },
    std::vector<std::string>{
      "", "'int'", "'float'", "'number'", "'string'", "'bool'", "'any'", 
      "'void'", "'null'", "'list'", "'map'", "'function'", "'coro'", "'mutivar'", 
      "'if'", "'else'", "'while'", "'for'", "'break'", "'continue'", "'return'", 
      "'true'", "'false'", "'const'", "'auto'", "'global'", "'static'", 
      "'import'", "'as'", "'type'", "'from'", "'private'", "'export'", "'class'", 
      "'new'", "'+'", "'-'", "'*'", "'/'", "'%'", "'='", "'+='", "'-='", 
      "'*='", "'/='", "'%='", "'..='", "'=='", "'!='", "'<'", "'>'", "'<='", 
      "'>='", "'&&'", "'||'", "'!'", "'..'", "'#'", "'&'", "'|'", "'^'", 
      "'~'", "'<<'", "'->'", "'('", "')'", "'['", "']'", "'{'", "'}'", "','", 
      "'.'", "':'", "';'", "'...'"
    },
    std::vector<std::string>{
      "", "INT", "FLOAT", "NUMBER", "STRING", "BOOL", "ANY", "VOID", "NULL_", 
      "LIST", "MAP", "FUNCTION", "COROUTINE", "MUTIVAR", "IF", "ELSE", "WHILE", 
      "FOR", "BREAK", "CONTINUE", "RETURN", "TRUE", "FALSE", "CONST", "AUTO", 
      "GLOBAL", "STATIC", "IMPORT", "AS", "TYPE", "FROM", "PRIVATE", "EXPORT", 
      "CLASS", "NEW", "ADD", "SUB", "MUL", "DIV", "MOD", "ASSIGN", "ADD_ASSIGN", 
      "SUB_ASSIGN", "MUL_ASSIGN", "DIV_ASSIGN", "MOD_ASSIGN", "CONCAT_ASSIGN", 
      "EQ", "NEQ", "LT", "GT", "LTE", "GTE", "AND", "OR", "NOT", "CONCAT", 
      "LEN", "BIT_AND", "BIT_OR", "BIT_XOR", "BIT_NOT", "LSHIFT", "ARROW", 
      "OP", "CP", "OSB", "CSB", "OCB", "CCB", "COMMA", "DOT", "COL", "SEMICOLON", 
      "DDD", "INTEGER", "FLOAT_LITERAL", "STRING_LITERAL", "IDENTIFIER", 
      "WS", "LINE_COMMENT", "BLOCK_COMMENT"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,81,720,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,2,56,7,
  	56,2,57,7,57,2,58,7,58,2,59,7,59,1,0,5,0,122,8,0,10,0,12,0,125,9,0,1,
  	0,1,0,1,1,1,1,5,1,131,8,1,10,1,12,1,134,9,1,1,1,1,1,1,2,1,2,1,2,1,2,1,
  	2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,158,
  	8,2,1,2,1,2,1,2,1,2,1,2,3,2,165,8,2,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,
  	3,1,3,1,3,5,3,178,8,3,10,3,12,3,181,9,3,1,3,1,3,1,3,1,3,3,3,187,8,3,1,
  	4,3,4,190,8,4,1,4,1,4,1,4,3,4,195,8,4,1,5,1,5,1,5,1,5,1,6,1,6,1,6,5,6,
  	204,8,6,10,6,12,6,207,9,6,1,6,1,6,1,6,1,6,5,6,213,8,6,10,6,12,6,216,9,
  	6,1,7,1,7,5,7,220,8,7,10,7,12,7,223,9,7,1,8,1,8,1,8,1,8,1,8,1,8,3,8,231,
  	8,8,1,9,3,9,234,8,9,1,9,1,9,1,9,1,9,1,9,3,9,241,8,9,1,10,3,10,244,8,10,
  	1,10,3,10,247,8,10,1,10,1,10,1,10,3,10,252,8,10,1,10,1,10,3,10,256,8,
  	10,1,10,3,10,259,8,10,1,10,1,10,1,10,3,10,264,8,10,1,10,3,10,267,8,10,
  	1,10,5,10,270,8,10,10,10,12,10,273,9,10,1,10,1,10,3,10,277,8,10,3,10,
  	279,8,10,1,11,1,11,3,11,283,8,11,1,11,1,11,1,12,3,12,288,8,12,1,12,1,
  	12,1,12,1,12,3,12,294,8,12,1,12,1,12,1,12,1,12,3,12,300,8,12,1,12,1,12,
  	1,12,1,12,3,12,306,8,12,1,12,1,12,1,12,3,12,311,8,12,1,13,1,13,1,13,1,
  	13,5,13,317,8,13,10,13,12,13,320,9,13,1,13,1,13,1,14,3,14,325,8,14,1,
  	14,3,14,328,8,14,1,14,1,14,1,14,3,14,333,8,14,1,14,3,14,336,8,14,1,14,
  	1,14,1,14,1,14,3,14,342,8,14,1,14,1,14,1,14,1,14,3,14,348,8,14,1,14,1,
  	14,1,14,1,14,3,14,354,8,14,1,14,1,14,1,14,3,14,359,8,14,1,15,1,15,1,15,
  	1,15,1,15,3,15,366,8,15,1,16,1,16,1,16,5,16,371,8,16,10,16,12,16,374,
  	9,16,1,17,1,17,1,18,1,18,1,18,1,18,1,18,3,18,383,8,18,1,19,1,19,1,19,
  	1,19,1,19,1,19,1,19,3,19,392,8,19,1,20,1,20,1,21,1,21,1,21,5,21,399,8,
  	21,10,21,12,21,402,9,21,1,22,1,22,1,22,5,22,407,8,22,10,22,12,22,410,
  	9,22,1,23,1,23,1,23,5,23,415,8,23,10,23,12,23,418,9,23,1,24,1,24,1,24,
  	5,24,423,8,24,10,24,12,24,426,9,24,1,25,1,25,1,25,5,25,431,8,25,10,25,
  	12,25,434,9,25,1,26,1,26,1,26,5,26,439,8,26,10,26,12,26,442,9,26,1,27,
  	1,27,1,27,1,27,5,27,448,8,27,10,27,12,27,451,9,27,1,28,1,28,1,29,1,29,
  	1,29,1,29,5,29,459,8,29,10,29,12,29,462,9,29,1,30,1,30,1,31,1,31,1,31,
  	1,31,5,31,470,8,31,10,31,12,31,473,9,31,1,32,1,32,1,32,3,32,478,8,32,
  	1,33,1,33,1,33,5,33,483,8,33,10,33,12,33,486,9,33,1,34,1,34,1,34,1,34,
  	5,34,492,8,34,10,34,12,34,495,9,34,1,35,1,35,1,36,1,36,1,36,1,36,5,36,
  	503,8,36,10,36,12,36,506,9,36,1,37,1,37,1,38,1,38,1,38,3,38,513,8,38,
  	1,39,1,39,5,39,517,8,39,10,39,12,39,520,9,39,1,40,1,40,1,40,1,40,1,40,
  	1,40,1,40,1,40,1,40,1,40,3,40,532,8,40,1,40,3,40,535,8,40,1,41,1,41,1,
  	41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,3,41,548,8,41,1,42,1,42,1,
  	43,1,43,1,43,3,43,555,8,43,1,43,1,43,1,43,1,43,3,43,561,8,43,1,43,1,43,
  	1,44,1,44,3,44,567,8,44,1,44,1,44,1,45,1,45,3,45,573,8,45,1,45,1,45,1,
  	46,1,46,1,46,5,46,580,8,46,10,46,12,46,583,9,46,1,47,1,47,1,47,1,47,1,
  	47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,3,47,597,8,47,1,48,1,48,1,48,1,
  	48,3,48,603,8,48,1,48,1,48,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,
  	49,1,49,1,49,1,49,5,49,619,8,49,10,49,12,49,622,9,49,1,49,1,49,3,49,626,
  	8,49,1,50,1,50,1,50,1,50,1,50,1,50,1,51,1,51,1,51,1,51,1,51,1,51,1,52,
  	1,52,1,52,3,52,643,8,52,1,52,1,52,3,52,647,8,52,1,52,1,52,1,52,5,52,652,
  	8,52,10,52,12,52,655,9,52,1,52,1,52,1,52,3,52,660,8,52,1,53,1,53,1,53,
  	5,53,665,8,53,10,53,12,53,668,9,53,1,54,1,54,1,54,3,54,673,8,54,1,55,
  	1,55,1,55,3,55,678,8,55,1,55,3,55,681,8,55,1,56,1,56,1,56,3,56,686,8,
  	56,1,56,1,56,1,56,1,56,3,56,692,8,56,5,56,694,8,56,10,56,12,56,697,9,
  	56,1,57,1,57,1,57,5,57,702,8,57,10,57,12,57,705,9,57,1,57,1,57,3,57,709,
  	8,57,1,57,3,57,712,8,57,1,58,1,58,1,58,1,59,3,59,718,8,59,1,59,0,0,60,
  	0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,
  	50,52,54,56,58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,
  	96,98,100,102,104,106,108,110,112,114,116,118,0,8,1,0,41,46,3,0,1,5,7,
  	8,11,12,1,0,47,48,1,0,49,52,1,0,35,36,1,0,37,39,4,0,36,36,55,55,57,57,
  	61,61,3,0,8,8,21,22,75,77,773,0,123,1,0,0,0,2,128,1,0,0,0,4,164,1,0,0,
  	0,6,186,1,0,0,0,8,189,1,0,0,0,10,196,1,0,0,0,12,200,1,0,0,0,14,217,1,
  	0,0,0,16,230,1,0,0,0,18,233,1,0,0,0,20,278,1,0,0,0,22,282,1,0,0,0,24,
  	310,1,0,0,0,26,312,1,0,0,0,28,358,1,0,0,0,30,365,1,0,0,0,32,367,1,0,0,
  	0,34,375,1,0,0,0,36,377,1,0,0,0,38,384,1,0,0,0,40,393,1,0,0,0,42,395,
  	1,0,0,0,44,403,1,0,0,0,46,411,1,0,0,0,48,419,1,0,0,0,50,427,1,0,0,0,52,
  	435,1,0,0,0,54,443,1,0,0,0,56,452,1,0,0,0,58,454,1,0,0,0,60,463,1,0,0,
  	0,62,465,1,0,0,0,64,477,1,0,0,0,66,479,1,0,0,0,68,487,1,0,0,0,70,496,
  	1,0,0,0,72,498,1,0,0,0,74,507,1,0,0,0,76,512,1,0,0,0,78,514,1,0,0,0,80,
  	534,1,0,0,0,82,547,1,0,0,0,84,549,1,0,0,0,86,551,1,0,0,0,88,564,1,0,0,
  	0,90,570,1,0,0,0,92,576,1,0,0,0,94,596,1,0,0,0,96,598,1,0,0,0,98,606,
  	1,0,0,0,100,627,1,0,0,0,102,633,1,0,0,0,104,659,1,0,0,0,106,661,1,0,0,
  	0,108,672,1,0,0,0,110,680,1,0,0,0,112,682,1,0,0,0,114,711,1,0,0,0,116,
  	713,1,0,0,0,118,717,1,0,0,0,120,122,3,4,2,0,121,120,1,0,0,0,122,125,1,
  	0,0,0,123,121,1,0,0,0,123,124,1,0,0,0,124,126,1,0,0,0,125,123,1,0,0,0,
  	126,127,5,0,0,1,127,1,1,0,0,0,128,132,5,68,0,0,129,131,3,4,2,0,130,129,
  	1,0,0,0,131,134,1,0,0,0,132,130,1,0,0,0,132,133,1,0,0,0,133,135,1,0,0,
  	0,134,132,1,0,0,0,135,136,5,69,0,0,136,3,1,0,0,0,137,165,5,73,0,0,138,
  	139,3,12,6,0,139,140,5,73,0,0,140,165,1,0,0,0,141,142,3,10,5,0,142,143,
  	5,73,0,0,143,165,1,0,0,0,144,145,3,40,20,0,145,146,5,73,0,0,146,165,1,
  	0,0,0,147,165,3,18,9,0,148,165,3,98,49,0,149,165,3,100,50,0,150,165,3,
  	102,51,0,151,152,5,18,0,0,152,165,5,73,0,0,153,154,5,19,0,0,154,165,5,
  	73,0,0,155,157,5,20,0,0,156,158,3,42,21,0,157,156,1,0,0,0,157,158,1,0,
  	0,0,158,159,1,0,0,0,159,165,5,73,0,0,160,165,3,2,1,0,161,162,3,6,3,0,
  	162,163,5,73,0,0,163,165,1,0,0,0,164,137,1,0,0,0,164,138,1,0,0,0,164,
  	141,1,0,0,0,164,144,1,0,0,0,164,147,1,0,0,0,164,148,1,0,0,0,164,149,1,
  	0,0,0,164,150,1,0,0,0,164,151,1,0,0,0,164,153,1,0,0,0,164,155,1,0,0,0,
  	164,160,1,0,0,0,164,161,1,0,0,0,165,5,1,0,0,0,166,167,5,27,0,0,167,168,
  	5,37,0,0,168,169,5,28,0,0,169,170,5,78,0,0,170,171,5,30,0,0,171,187,5,
  	77,0,0,172,173,5,27,0,0,173,174,5,68,0,0,174,179,3,8,4,0,175,176,5,70,
  	0,0,176,178,3,8,4,0,177,175,1,0,0,0,178,181,1,0,0,0,179,177,1,0,0,0,179,
  	180,1,0,0,0,180,182,1,0,0,0,181,179,1,0,0,0,182,183,5,69,0,0,183,184,
  	5,30,0,0,184,185,5,77,0,0,185,187,1,0,0,0,186,166,1,0,0,0,186,172,1,0,
  	0,0,187,7,1,0,0,0,188,190,5,29,0,0,189,188,1,0,0,0,189,190,1,0,0,0,190,
  	191,1,0,0,0,191,194,5,78,0,0,192,193,5,28,0,0,193,195,5,78,0,0,194,192,
  	1,0,0,0,194,195,1,0,0,0,195,9,1,0,0,0,196,197,3,14,7,0,197,198,7,0,0,
  	0,198,199,3,40,20,0,199,11,1,0,0,0,200,205,3,14,7,0,201,202,5,70,0,0,
  	202,204,3,14,7,0,203,201,1,0,0,0,204,207,1,0,0,0,205,203,1,0,0,0,205,
  	206,1,0,0,0,206,208,1,0,0,0,207,205,1,0,0,0,208,209,5,40,0,0,209,214,
  	3,40,20,0,210,211,5,70,0,0,211,213,3,40,20,0,212,210,1,0,0,0,213,216,
  	1,0,0,0,214,212,1,0,0,0,214,215,1,0,0,0,215,13,1,0,0,0,216,214,1,0,0,
  	0,217,221,5,78,0,0,218,220,3,16,8,0,219,218,1,0,0,0,220,223,1,0,0,0,221,
  	219,1,0,0,0,221,222,1,0,0,0,222,15,1,0,0,0,223,221,1,0,0,0,224,225,5,
  	66,0,0,225,226,3,40,20,0,226,227,5,67,0,0,227,231,1,0,0,0,228,229,5,71,
  	0,0,229,231,5,78,0,0,230,224,1,0,0,0,230,228,1,0,0,0,231,17,1,0,0,0,232,
  	234,5,32,0,0,233,232,1,0,0,0,233,234,1,0,0,0,234,240,1,0,0,0,235,236,
  	3,20,10,0,236,237,5,73,0,0,237,241,1,0,0,0,238,241,3,24,12,0,239,241,
  	3,26,13,0,240,235,1,0,0,0,240,238,1,0,0,0,240,239,1,0,0,0,241,19,1,0,
  	0,0,242,244,5,25,0,0,243,242,1,0,0,0,243,244,1,0,0,0,244,246,1,0,0,0,
  	245,247,5,23,0,0,246,245,1,0,0,0,246,247,1,0,0,0,247,248,1,0,0,0,248,
  	251,3,22,11,0,249,250,5,40,0,0,250,252,3,40,20,0,251,249,1,0,0,0,251,
  	252,1,0,0,0,252,279,1,0,0,0,253,255,5,13,0,0,254,256,5,25,0,0,255,254,
  	1,0,0,0,255,256,1,0,0,0,256,258,1,0,0,0,257,259,5,23,0,0,258,257,1,0,
  	0,0,258,259,1,0,0,0,259,260,1,0,0,0,260,271,5,78,0,0,261,263,5,70,0,0,
  	262,264,5,25,0,0,263,262,1,0,0,0,263,264,1,0,0,0,264,266,1,0,0,0,265,
  	267,5,23,0,0,266,265,1,0,0,0,266,267,1,0,0,0,267,268,1,0,0,0,268,270,
  	5,78,0,0,269,261,1,0,0,0,270,273,1,0,0,0,271,269,1,0,0,0,271,272,1,0,
  	0,0,272,276,1,0,0,0,273,271,1,0,0,0,274,275,5,40,0,0,275,277,3,40,20,
  	0,276,274,1,0,0,0,276,277,1,0,0,0,277,279,1,0,0,0,278,243,1,0,0,0,278,
  	253,1,0,0,0,279,21,1,0,0,0,280,283,3,30,15,0,281,283,5,24,0,0,282,280,
  	1,0,0,0,282,281,1,0,0,0,283,284,1,0,0,0,284,285,5,78,0,0,285,23,1,0,0,
  	0,286,288,5,25,0,0,287,286,1,0,0,0,287,288,1,0,0,0,288,289,1,0,0,0,289,
  	290,3,30,15,0,290,291,3,32,16,0,291,293,5,64,0,0,292,294,3,114,57,0,293,
  	292,1,0,0,0,293,294,1,0,0,0,294,295,1,0,0,0,295,296,5,65,0,0,296,297,
  	3,2,1,0,297,311,1,0,0,0,298,300,5,25,0,0,299,298,1,0,0,0,299,300,1,0,
  	0,0,300,301,1,0,0,0,301,302,5,13,0,0,302,303,3,32,16,0,303,305,5,64,0,
  	0,304,306,3,114,57,0,305,304,1,0,0,0,305,306,1,0,0,0,306,307,1,0,0,0,
  	307,308,5,65,0,0,308,309,3,2,1,0,309,311,1,0,0,0,310,287,1,0,0,0,310,
  	299,1,0,0,0,311,25,1,0,0,0,312,313,5,33,0,0,313,314,5,78,0,0,314,318,
  	5,68,0,0,315,317,3,28,14,0,316,315,1,0,0,0,317,320,1,0,0,0,318,316,1,
  	0,0,0,318,319,1,0,0,0,319,321,1,0,0,0,320,318,1,0,0,0,321,322,5,69,0,
  	0,322,27,1,0,0,0,323,325,5,26,0,0,324,323,1,0,0,0,324,325,1,0,0,0,325,
  	327,1,0,0,0,326,328,5,23,0,0,327,326,1,0,0,0,327,328,1,0,0,0,328,329,
  	1,0,0,0,329,332,3,22,11,0,330,331,5,40,0,0,331,333,3,40,20,0,332,330,
  	1,0,0,0,332,333,1,0,0,0,333,359,1,0,0,0,334,336,5,26,0,0,335,334,1,0,
  	0,0,335,336,1,0,0,0,336,337,1,0,0,0,337,338,3,30,15,0,338,339,5,78,0,
  	0,339,341,5,64,0,0,340,342,3,114,57,0,341,340,1,0,0,0,341,342,1,0,0,0,
  	342,343,1,0,0,0,343,344,5,65,0,0,344,345,3,2,1,0,345,359,1,0,0,0,346,
  	348,5,26,0,0,347,346,1,0,0,0,347,348,1,0,0,0,348,349,1,0,0,0,349,350,
  	5,13,0,0,350,351,5,78,0,0,351,353,5,64,0,0,352,354,3,114,57,0,353,352,
  	1,0,0,0,353,354,1,0,0,0,354,355,1,0,0,0,355,356,5,65,0,0,356,359,3,2,
  	1,0,357,359,5,73,0,0,358,324,1,0,0,0,358,335,1,0,0,0,358,347,1,0,0,0,
  	358,357,1,0,0,0,359,29,1,0,0,0,360,366,3,34,17,0,361,366,3,36,18,0,362,
  	366,3,38,19,0,363,366,5,6,0,0,364,366,3,32,16,0,365,360,1,0,0,0,365,361,
  	1,0,0,0,365,362,1,0,0,0,365,363,1,0,0,0,365,364,1,0,0,0,366,31,1,0,0,
  	0,367,372,5,78,0,0,368,369,5,71,0,0,369,371,5,78,0,0,370,368,1,0,0,0,
  	371,374,1,0,0,0,372,370,1,0,0,0,372,373,1,0,0,0,373,33,1,0,0,0,374,372,
  	1,0,0,0,375,376,7,1,0,0,376,35,1,0,0,0,377,382,5,9,0,0,378,379,5,49,0,
  	0,379,380,3,30,15,0,380,381,5,50,0,0,381,383,1,0,0,0,382,378,1,0,0,0,
  	382,383,1,0,0,0,383,37,1,0,0,0,384,391,5,10,0,0,385,386,5,49,0,0,386,
  	387,3,30,15,0,387,388,5,70,0,0,388,389,3,30,15,0,389,390,5,50,0,0,390,
  	392,1,0,0,0,391,385,1,0,0,0,391,392,1,0,0,0,392,39,1,0,0,0,393,394,3,
  	44,22,0,394,41,1,0,0,0,395,400,3,40,20,0,396,397,5,70,0,0,397,399,3,40,
  	20,0,398,396,1,0,0,0,399,402,1,0,0,0,400,398,1,0,0,0,400,401,1,0,0,0,
  	401,43,1,0,0,0,402,400,1,0,0,0,403,408,3,46,23,0,404,405,5,54,0,0,405,
  	407,3,46,23,0,406,404,1,0,0,0,407,410,1,0,0,0,408,406,1,0,0,0,408,409,
  	1,0,0,0,409,45,1,0,0,0,410,408,1,0,0,0,411,416,3,48,24,0,412,413,5,53,
  	0,0,413,415,3,48,24,0,414,412,1,0,0,0,415,418,1,0,0,0,416,414,1,0,0,0,
  	416,417,1,0,0,0,417,47,1,0,0,0,418,416,1,0,0,0,419,424,3,50,25,0,420,
  	421,5,59,0,0,421,423,3,50,25,0,422,420,1,0,0,0,423,426,1,0,0,0,424,422,
  	1,0,0,0,424,425,1,0,0,0,425,49,1,0,0,0,426,424,1,0,0,0,427,432,3,52,26,
  	0,428,429,5,60,0,0,429,431,3,52,26,0,430,428,1,0,0,0,431,434,1,0,0,0,
  	432,430,1,0,0,0,432,433,1,0,0,0,433,51,1,0,0,0,434,432,1,0,0,0,435,440,
  	3,54,27,0,436,437,5,58,0,0,437,439,3,54,27,0,438,436,1,0,0,0,439,442,
  	1,0,0,0,440,438,1,0,0,0,440,441,1,0,0,0,441,53,1,0,0,0,442,440,1,0,0,
  	0,443,449,3,58,29,0,444,445,3,56,28,0,445,446,3,58,29,0,446,448,1,0,0,
  	0,447,444,1,0,0,0,448,451,1,0,0,0,449,447,1,0,0,0,449,450,1,0,0,0,450,
  	55,1,0,0,0,451,449,1,0,0,0,452,453,7,2,0,0,453,57,1,0,0,0,454,460,3,62,
  	31,0,455,456,3,60,30,0,456,457,3,62,31,0,457,459,1,0,0,0,458,455,1,0,
  	0,0,459,462,1,0,0,0,460,458,1,0,0,0,460,461,1,0,0,0,461,59,1,0,0,0,462,
  	460,1,0,0,0,463,464,7,3,0,0,464,61,1,0,0,0,465,471,3,66,33,0,466,467,
  	3,64,32,0,467,468,3,66,33,0,468,470,1,0,0,0,469,466,1,0,0,0,470,473,1,
  	0,0,0,471,469,1,0,0,0,471,472,1,0,0,0,472,63,1,0,0,0,473,471,1,0,0,0,
  	474,478,5,62,0,0,475,476,5,50,0,0,476,478,5,50,0,0,477,474,1,0,0,0,477,
  	475,1,0,0,0,478,65,1,0,0,0,479,484,3,68,34,0,480,481,5,56,0,0,481,483,
  	3,68,34,0,482,480,1,0,0,0,483,486,1,0,0,0,484,482,1,0,0,0,484,485,1,0,
  	0,0,485,67,1,0,0,0,486,484,1,0,0,0,487,493,3,72,36,0,488,489,3,70,35,
  	0,489,490,3,72,36,0,490,492,1,0,0,0,491,488,1,0,0,0,492,495,1,0,0,0,493,
  	491,1,0,0,0,493,494,1,0,0,0,494,69,1,0,0,0,495,493,1,0,0,0,496,497,7,
  	4,0,0,497,71,1,0,0,0,498,504,3,76,38,0,499,500,3,74,37,0,500,501,3,76,
  	38,0,501,503,1,0,0,0,502,499,1,0,0,0,503,506,1,0,0,0,504,502,1,0,0,0,
  	504,505,1,0,0,0,505,73,1,0,0,0,506,504,1,0,0,0,507,508,7,5,0,0,508,75,
  	1,0,0,0,509,510,7,6,0,0,510,513,3,76,38,0,511,513,3,78,39,0,512,509,1,
  	0,0,0,512,511,1,0,0,0,513,77,1,0,0,0,514,518,3,82,41,0,515,517,3,80,40,
  	0,516,515,1,0,0,0,517,520,1,0,0,0,518,516,1,0,0,0,518,519,1,0,0,0,519,
  	79,1,0,0,0,520,518,1,0,0,0,521,522,5,66,0,0,522,523,3,40,20,0,523,524,
  	5,67,0,0,524,535,1,0,0,0,525,526,5,71,0,0,526,535,5,78,0,0,527,528,5,
  	72,0,0,528,535,5,78,0,0,529,531,5,64,0,0,530,532,3,118,59,0,531,530,1,
  	0,0,0,531,532,1,0,0,0,532,533,1,0,0,0,533,535,5,65,0,0,534,521,1,0,0,
  	0,534,525,1,0,0,0,534,527,1,0,0,0,534,529,1,0,0,0,535,81,1,0,0,0,536,
  	548,3,84,42,0,537,548,3,88,44,0,538,548,3,90,45,0,539,548,5,78,0,0,540,
  	548,5,74,0,0,541,542,5,64,0,0,542,543,3,40,20,0,543,544,5,65,0,0,544,
  	548,1,0,0,0,545,548,3,96,48,0,546,548,3,86,43,0,547,536,1,0,0,0,547,537,
  	1,0,0,0,547,538,1,0,0,0,547,539,1,0,0,0,547,540,1,0,0,0,547,541,1,0,0,
  	0,547,545,1,0,0,0,547,546,1,0,0,0,548,83,1,0,0,0,549,550,7,7,0,0,550,
  	85,1,0,0,0,551,552,5,11,0,0,552,554,5,64,0,0,553,555,3,114,57,0,554,553,
  	1,0,0,0,554,555,1,0,0,0,555,556,1,0,0,0,556,557,5,65,0,0,557,560,5,63,
  	0,0,558,561,3,30,15,0,559,561,5,13,0,0,560,558,1,0,0,0,560,559,1,0,0,
  	0,561,562,1,0,0,0,562,563,3,2,1,0,563,87,1,0,0,0,564,566,5,66,0,0,565,
  	567,3,42,21,0,566,565,1,0,0,0,566,567,1,0,0,0,567,568,1,0,0,0,568,569,
  	5,67,0,0,569,89,1,0,0,0,570,572,5,68,0,0,571,573,3,92,46,0,572,571,1,
  	0,0,0,572,573,1,0,0,0,573,574,1,0,0,0,574,575,5,69,0,0,575,91,1,0,0,0,
  	576,581,3,94,47,0,577,578,5,70,0,0,578,580,3,94,47,0,579,577,1,0,0,0,
  	580,583,1,0,0,0,581,579,1,0,0,0,581,582,1,0,0,0,582,93,1,0,0,0,583,581,
  	1,0,0,0,584,585,5,78,0,0,585,586,5,72,0,0,586,597,3,40,20,0,587,588,5,
  	66,0,0,588,589,3,40,20,0,589,590,5,67,0,0,590,591,5,72,0,0,591,592,3,
  	40,20,0,592,597,1,0,0,0,593,594,5,77,0,0,594,595,5,72,0,0,595,597,3,40,
  	20,0,596,584,1,0,0,0,596,587,1,0,0,0,596,593,1,0,0,0,597,95,1,0,0,0,598,
  	599,5,34,0,0,599,600,3,32,16,0,600,602,5,64,0,0,601,603,3,118,59,0,602,
  	601,1,0,0,0,602,603,1,0,0,0,603,604,1,0,0,0,604,605,5,65,0,0,605,97,1,
  	0,0,0,606,607,5,14,0,0,607,608,5,64,0,0,608,609,3,40,20,0,609,610,5,65,
  	0,0,610,620,3,2,1,0,611,612,5,15,0,0,612,613,5,14,0,0,613,614,5,64,0,
  	0,614,615,3,40,20,0,615,616,5,65,0,0,616,617,3,2,1,0,617,619,1,0,0,0,
  	618,611,1,0,0,0,619,622,1,0,0,0,620,618,1,0,0,0,620,621,1,0,0,0,621,625,
  	1,0,0,0,622,620,1,0,0,0,623,624,5,15,0,0,624,626,3,2,1,0,625,623,1,0,
  	0,0,625,626,1,0,0,0,626,99,1,0,0,0,627,628,5,16,0,0,628,629,5,64,0,0,
  	629,630,3,40,20,0,630,631,5,65,0,0,631,632,3,2,1,0,632,101,1,0,0,0,633,
  	634,5,17,0,0,634,635,5,64,0,0,635,636,3,104,52,0,636,637,5,65,0,0,637,
  	638,3,2,1,0,638,103,1,0,0,0,639,640,3,110,55,0,640,642,5,73,0,0,641,643,
  	3,40,20,0,642,641,1,0,0,0,642,643,1,0,0,0,643,644,1,0,0,0,644,646,5,73,
  	0,0,645,647,3,106,53,0,646,645,1,0,0,0,646,647,1,0,0,0,647,660,1,0,0,
  	0,648,653,3,22,11,0,649,650,5,70,0,0,650,652,3,22,11,0,651,649,1,0,0,
  	0,652,655,1,0,0,0,653,651,1,0,0,0,653,654,1,0,0,0,654,656,1,0,0,0,655,
  	653,1,0,0,0,656,657,5,72,0,0,657,658,3,40,20,0,658,660,1,0,0,0,659,639,
  	1,0,0,0,659,648,1,0,0,0,660,105,1,0,0,0,661,666,3,108,54,0,662,663,5,
  	70,0,0,663,665,3,108,54,0,664,662,1,0,0,0,665,668,1,0,0,0,666,664,1,0,
  	0,0,666,667,1,0,0,0,667,107,1,0,0,0,668,666,1,0,0,0,669,673,3,40,20,0,
  	670,673,3,10,5,0,671,673,3,12,6,0,672,669,1,0,0,0,672,670,1,0,0,0,672,
  	671,1,0,0,0,673,109,1,0,0,0,674,681,3,112,56,0,675,681,3,12,6,0,676,678,
  	3,42,21,0,677,676,1,0,0,0,677,678,1,0,0,0,678,681,1,0,0,0,679,681,1,0,
  	0,0,680,674,1,0,0,0,680,675,1,0,0,0,680,677,1,0,0,0,680,679,1,0,0,0,681,
  	111,1,0,0,0,682,685,3,22,11,0,683,684,5,40,0,0,684,686,3,40,20,0,685,
  	683,1,0,0,0,685,686,1,0,0,0,686,695,1,0,0,0,687,688,5,70,0,0,688,691,
  	3,22,11,0,689,690,5,40,0,0,690,692,3,40,20,0,691,689,1,0,0,0,691,692,
  	1,0,0,0,692,694,1,0,0,0,693,687,1,0,0,0,694,697,1,0,0,0,695,693,1,0,0,
  	0,695,696,1,0,0,0,696,113,1,0,0,0,697,695,1,0,0,0,698,703,3,116,58,0,
  	699,700,5,70,0,0,700,702,3,116,58,0,701,699,1,0,0,0,702,705,1,0,0,0,703,
  	701,1,0,0,0,703,704,1,0,0,0,704,708,1,0,0,0,705,703,1,0,0,0,706,707,5,
  	70,0,0,707,709,5,74,0,0,708,706,1,0,0,0,708,709,1,0,0,0,709,712,1,0,0,
  	0,710,712,5,74,0,0,711,698,1,0,0,0,711,710,1,0,0,0,712,115,1,0,0,0,713,
  	714,3,30,15,0,714,715,5,78,0,0,715,117,1,0,0,0,716,718,3,42,21,0,717,
  	716,1,0,0,0,717,718,1,0,0,0,718,119,1,0,0,0,85,123,132,157,164,179,186,
  	189,194,205,214,221,230,233,240,243,246,251,255,258,263,266,271,276,278,
  	282,287,293,299,305,310,318,324,327,332,335,341,347,353,358,365,372,382,
  	391,400,408,416,424,432,440,449,460,471,477,484,493,504,512,518,531,534,
  	547,554,560,566,572,581,596,602,620,625,642,646,653,659,666,672,677,680,
  	685,691,695,703,708,711,717
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  langparserParserStaticData = staticData.release();
}

}

LangParser::LangParser(TokenStream *input) : LangParser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

LangParser::LangParser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  LangParser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *langparserParserStaticData->atn, langparserParserStaticData->decisionToDFA, langparserParserStaticData->sharedContextCache, options);
}

LangParser::~LangParser() {
  delete _interpreter;
}

const atn::ATN& LangParser::getATN() const {
  return *langparserParserStaticData->atn;
}

std::string LangParser::getGrammarFileName() const {
  return "LangParser.g4";
}

const std::vector<std::string>& LangParser::getRuleNames() const {
  return langparserParserStaticData->ruleNames;
}

const dfa::Vocabulary& LangParser::getVocabulary() const {
  return langparserParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView LangParser::getSerializedATN() const {
  return langparserParserStaticData->serializedATN;
}


//----------------- CompilationUnitContext ------------------------------------------------------------------

LangParser::CompilationUnitContext::CompilationUnitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::CompilationUnitContext::EOF() {
  return getToken(LangParser::EOF, 0);
}

std::vector<LangParser::StatementContext *> LangParser::CompilationUnitContext::statement() {
  return getRuleContexts<LangParser::StatementContext>();
}

LangParser::StatementContext* LangParser::CompilationUnitContext::statement(size_t i) {
  return getRuleContext<LangParser::StatementContext>(i);
}


size_t LangParser::CompilationUnitContext::getRuleIndex() const {
  return LangParser::RuleCompilationUnit;
}


std::any LangParser::CompilationUnitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitCompilationUnit(this);
  else
    return visitor->visitChildren(this);
}

LangParser::CompilationUnitContext* LangParser::compilationUnit() {
  CompilationUnitContext *_localctx = _tracker.createInstance<CompilationUnitContext>(_ctx, getState());
  enterRule(_localctx, 0, LangParser::RuleCompilationUnit);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(123);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2485987093294055422) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 32277) != 0)) {
      setState(120);
      statement();
      setState(125);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(126);
    match(LangParser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BlockStatementContext ------------------------------------------------------------------

LangParser::BlockStatementContext::BlockStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::BlockStatementContext::OCB() {
  return getToken(LangParser::OCB, 0);
}

tree::TerminalNode* LangParser::BlockStatementContext::CCB() {
  return getToken(LangParser::CCB, 0);
}

std::vector<LangParser::StatementContext *> LangParser::BlockStatementContext::statement() {
  return getRuleContexts<LangParser::StatementContext>();
}

LangParser::StatementContext* LangParser::BlockStatementContext::statement(size_t i) {
  return getRuleContext<LangParser::StatementContext>(i);
}


size_t LangParser::BlockStatementContext::getRuleIndex() const {
  return LangParser::RuleBlockStatement;
}


std::any LangParser::BlockStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitBlockStatement(this);
  else
    return visitor->visitChildren(this);
}

LangParser::BlockStatementContext* LangParser::blockStatement() {
  BlockStatementContext *_localctx = _tracker.createInstance<BlockStatementContext>(_ctx, getState());
  enterRule(_localctx, 2, LangParser::RuleBlockStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(128);
    match(LangParser::OCB);
    setState(132);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2485987093294055422) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 32277) != 0)) {
      setState(129);
      statement();
      setState(134);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(135);
    match(LangParser::CCB);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StatementContext ------------------------------------------------------------------

LangParser::StatementContext::StatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::StatementContext::getRuleIndex() const {
  return LangParser::RuleStatement;
}

void LangParser::StatementContext::copyFrom(StatementContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ImportStmtContext ------------------------------------------------------------------

LangParser::ImportStatementContext* LangParser::ImportStmtContext::importStatement() {
  return getRuleContext<LangParser::ImportStatementContext>(0);
}

tree::TerminalNode* LangParser::ImportStmtContext::SEMICOLON() {
  return getToken(LangParser::SEMICOLON, 0);
}

LangParser::ImportStmtContext::ImportStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::ImportStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitImportStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ForStmtContext ------------------------------------------------------------------

LangParser::ForStatementContext* LangParser::ForStmtContext::forStatement() {
  return getRuleContext<LangParser::ForStatementContext>(0);
}

LangParser::ForStmtContext::ForStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::ForStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- WhileStmtContext ------------------------------------------------------------------

LangParser::WhileStatementContext* LangParser::WhileStmtContext::whileStatement() {
  return getRuleContext<LangParser::WhileStatementContext>(0);
}

LangParser::WhileStmtContext::WhileStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::WhileStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitWhileStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AssignStmtContext ------------------------------------------------------------------

LangParser::AssignStatementContext* LangParser::AssignStmtContext::assignStatement() {
  return getRuleContext<LangParser::AssignStatementContext>(0);
}

tree::TerminalNode* LangParser::AssignStmtContext::SEMICOLON() {
  return getToken(LangParser::SEMICOLON, 0);
}

LangParser::AssignStmtContext::AssignStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::AssignStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitAssignStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ExpressionStmtContext ------------------------------------------------------------------

LangParser::ExpressionContext* LangParser::ExpressionStmtContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

tree::TerminalNode* LangParser::ExpressionStmtContext::SEMICOLON() {
  return getToken(LangParser::SEMICOLON, 0);
}

LangParser::ExpressionStmtContext::ExpressionStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::ExpressionStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitExpressionStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ReturnStmtContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::ReturnStmtContext::RETURN() {
  return getToken(LangParser::RETURN, 0);
}

tree::TerminalNode* LangParser::ReturnStmtContext::SEMICOLON() {
  return getToken(LangParser::SEMICOLON, 0);
}

LangParser::ExpressionListContext* LangParser::ReturnStmtContext::expressionList() {
  return getRuleContext<LangParser::ExpressionListContext>(0);
}

LangParser::ReturnStmtContext::ReturnStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::ReturnStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitReturnStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- UpdateStmtContext ------------------------------------------------------------------

LangParser::UpdateStatementContext* LangParser::UpdateStmtContext::updateStatement() {
  return getRuleContext<LangParser::UpdateStatementContext>(0);
}

tree::TerminalNode* LangParser::UpdateStmtContext::SEMICOLON() {
  return getToken(LangParser::SEMICOLON, 0);
}

LangParser::UpdateStmtContext::UpdateStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::UpdateStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitUpdateStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- DeclarationStmtContext ------------------------------------------------------------------

LangParser::DeclarationContext* LangParser::DeclarationStmtContext::declaration() {
  return getRuleContext<LangParser::DeclarationContext>(0);
}

LangParser::DeclarationStmtContext::DeclarationStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::DeclarationStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitDeclarationStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- SemicolonStmtContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::SemicolonStmtContext::SEMICOLON() {
  return getToken(LangParser::SEMICOLON, 0);
}

LangParser::SemicolonStmtContext::SemicolonStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::SemicolonStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitSemicolonStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IfStmtContext ------------------------------------------------------------------

LangParser::IfStatementContext* LangParser::IfStmtContext::ifStatement() {
  return getRuleContext<LangParser::IfStatementContext>(0);
}

LangParser::IfStmtContext::IfStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::IfStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitIfStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- BlockStmtContext ------------------------------------------------------------------

LangParser::BlockStatementContext* LangParser::BlockStmtContext::blockStatement() {
  return getRuleContext<LangParser::BlockStatementContext>(0);
}

LangParser::BlockStmtContext::BlockStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::BlockStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitBlockStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- BreakStmtContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::BreakStmtContext::BREAK() {
  return getToken(LangParser::BREAK, 0);
}

tree::TerminalNode* LangParser::BreakStmtContext::SEMICOLON() {
  return getToken(LangParser::SEMICOLON, 0);
}

LangParser::BreakStmtContext::BreakStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::BreakStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitBreakStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ContinueStmtContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::ContinueStmtContext::CONTINUE() {
  return getToken(LangParser::CONTINUE, 0);
}

tree::TerminalNode* LangParser::ContinueStmtContext::SEMICOLON() {
  return getToken(LangParser::SEMICOLON, 0);
}

LangParser::ContinueStmtContext::ContinueStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::ContinueStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitContinueStmt(this);
  else
    return visitor->visitChildren(this);
}
LangParser::StatementContext* LangParser::statement() {
  StatementContext *_localctx = _tracker.createInstance<StatementContext>(_ctx, getState());
  enterRule(_localctx, 4, LangParser::RuleStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(164);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 3, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::SemicolonStmtContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(137);
      match(LangParser::SEMICOLON);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<LangParser::AssignStmtContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(138);
      assignStatement();
      setState(139);
      match(LangParser::SEMICOLON);
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<LangParser::UpdateStmtContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(141);
      updateStatement();
      setState(142);
      match(LangParser::SEMICOLON);
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<LangParser::ExpressionStmtContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(144);
      expression();
      setState(145);
      match(LangParser::SEMICOLON);
      break;
    }

    case 5: {
      _localctx = _tracker.createInstance<LangParser::DeclarationStmtContext>(_localctx);
      enterOuterAlt(_localctx, 5);
      setState(147);
      declaration();
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<LangParser::IfStmtContext>(_localctx);
      enterOuterAlt(_localctx, 6);
      setState(148);
      ifStatement();
      break;
    }

    case 7: {
      _localctx = _tracker.createInstance<LangParser::WhileStmtContext>(_localctx);
      enterOuterAlt(_localctx, 7);
      setState(149);
      whileStatement();
      break;
    }

    case 8: {
      _localctx = _tracker.createInstance<LangParser::ForStmtContext>(_localctx);
      enterOuterAlt(_localctx, 8);
      setState(150);
      forStatement();
      break;
    }

    case 9: {
      _localctx = _tracker.createInstance<LangParser::BreakStmtContext>(_localctx);
      enterOuterAlt(_localctx, 9);
      setState(151);
      match(LangParser::BREAK);
      setState(152);
      match(LangParser::SEMICOLON);
      break;
    }

    case 10: {
      _localctx = _tracker.createInstance<LangParser::ContinueStmtContext>(_localctx);
      enterOuterAlt(_localctx, 10);
      setState(153);
      match(LangParser::CONTINUE);
      setState(154);
      match(LangParser::SEMICOLON);
      break;
    }

    case 11: {
      _localctx = _tracker.createInstance<LangParser::ReturnStmtContext>(_localctx);
      enterOuterAlt(_localctx, 11);
      setState(155);
      match(LangParser::RETURN);
      setState(157);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 2485987080214153472) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 64)) & 31765) != 0)) {
        setState(156);
        expressionList();
      }
      setState(159);
      match(LangParser::SEMICOLON);
      break;
    }

    case 12: {
      _localctx = _tracker.createInstance<LangParser::BlockStmtContext>(_localctx);
      enterOuterAlt(_localctx, 12);
      setState(160);
      blockStatement();
      break;
    }

    case 13: {
      _localctx = _tracker.createInstance<LangParser::ImportStmtContext>(_localctx);
      enterOuterAlt(_localctx, 13);
      setState(161);
      importStatement();
      setState(162);
      match(LangParser::SEMICOLON);
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ImportStatementContext ------------------------------------------------------------------

LangParser::ImportStatementContext::ImportStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::ImportStatementContext::getRuleIndex() const {
  return LangParser::RuleImportStatement;
}

void LangParser::ImportStatementContext::copyFrom(ImportStatementContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ImportNamespaceStmtContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::ImportNamespaceStmtContext::IMPORT() {
  return getToken(LangParser::IMPORT, 0);
}

tree::TerminalNode* LangParser::ImportNamespaceStmtContext::MUL() {
  return getToken(LangParser::MUL, 0);
}

tree::TerminalNode* LangParser::ImportNamespaceStmtContext::AS() {
  return getToken(LangParser::AS, 0);
}

tree::TerminalNode* LangParser::ImportNamespaceStmtContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

tree::TerminalNode* LangParser::ImportNamespaceStmtContext::FROM() {
  return getToken(LangParser::FROM, 0);
}

tree::TerminalNode* LangParser::ImportNamespaceStmtContext::STRING_LITERAL() {
  return getToken(LangParser::STRING_LITERAL, 0);
}

LangParser::ImportNamespaceStmtContext::ImportNamespaceStmtContext(ImportStatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::ImportNamespaceStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitImportNamespaceStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ImportNamedStmtContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::ImportNamedStmtContext::IMPORT() {
  return getToken(LangParser::IMPORT, 0);
}

tree::TerminalNode* LangParser::ImportNamedStmtContext::OCB() {
  return getToken(LangParser::OCB, 0);
}

std::vector<LangParser::ImportSpecifierContext *> LangParser::ImportNamedStmtContext::importSpecifier() {
  return getRuleContexts<LangParser::ImportSpecifierContext>();
}

LangParser::ImportSpecifierContext* LangParser::ImportNamedStmtContext::importSpecifier(size_t i) {
  return getRuleContext<LangParser::ImportSpecifierContext>(i);
}

tree::TerminalNode* LangParser::ImportNamedStmtContext::CCB() {
  return getToken(LangParser::CCB, 0);
}

tree::TerminalNode* LangParser::ImportNamedStmtContext::FROM() {
  return getToken(LangParser::FROM, 0);
}

tree::TerminalNode* LangParser::ImportNamedStmtContext::STRING_LITERAL() {
  return getToken(LangParser::STRING_LITERAL, 0);
}

std::vector<tree::TerminalNode *> LangParser::ImportNamedStmtContext::COMMA() {
  return getTokens(LangParser::COMMA);
}

tree::TerminalNode* LangParser::ImportNamedStmtContext::COMMA(size_t i) {
  return getToken(LangParser::COMMA, i);
}

LangParser::ImportNamedStmtContext::ImportNamedStmtContext(ImportStatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::ImportNamedStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitImportNamedStmt(this);
  else
    return visitor->visitChildren(this);
}
LangParser::ImportStatementContext* LangParser::importStatement() {
  ImportStatementContext *_localctx = _tracker.createInstance<ImportStatementContext>(_ctx, getState());
  enterRule(_localctx, 6, LangParser::RuleImportStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(186);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 5, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::ImportNamespaceStmtContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(166);
      match(LangParser::IMPORT);
      setState(167);
      match(LangParser::MUL);
      setState(168);
      match(LangParser::AS);
      setState(169);
      match(LangParser::IDENTIFIER);
      setState(170);
      match(LangParser::FROM);
      setState(171);
      match(LangParser::STRING_LITERAL);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<LangParser::ImportNamedStmtContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(172);
      match(LangParser::IMPORT);
      setState(173);
      match(LangParser::OCB);
      setState(174);
      importSpecifier();
      setState(179);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == LangParser::COMMA) {
        setState(175);
        match(LangParser::COMMA);
        setState(176);
        importSpecifier();
        setState(181);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(182);
      match(LangParser::CCB);
      setState(183);
      match(LangParser::FROM);
      setState(184);
      match(LangParser::STRING_LITERAL);
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ImportSpecifierContext ------------------------------------------------------------------

LangParser::ImportSpecifierContext::ImportSpecifierContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> LangParser::ImportSpecifierContext::IDENTIFIER() {
  return getTokens(LangParser::IDENTIFIER);
}

tree::TerminalNode* LangParser::ImportSpecifierContext::IDENTIFIER(size_t i) {
  return getToken(LangParser::IDENTIFIER, i);
}

tree::TerminalNode* LangParser::ImportSpecifierContext::TYPE() {
  return getToken(LangParser::TYPE, 0);
}

tree::TerminalNode* LangParser::ImportSpecifierContext::AS() {
  return getToken(LangParser::AS, 0);
}


size_t LangParser::ImportSpecifierContext::getRuleIndex() const {
  return LangParser::RuleImportSpecifier;
}


std::any LangParser::ImportSpecifierContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitImportSpecifier(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ImportSpecifierContext* LangParser::importSpecifier() {
  ImportSpecifierContext *_localctx = _tracker.createInstance<ImportSpecifierContext>(_ctx, getState());
  enterRule(_localctx, 8, LangParser::RuleImportSpecifier);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(189);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::TYPE) {
      setState(188);
      match(LangParser::TYPE);
    }
    setState(191);
    match(LangParser::IDENTIFIER);
    setState(194);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::AS) {
      setState(192);
      match(LangParser::AS);
      setState(193);
      match(LangParser::IDENTIFIER);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UpdateStatementContext ------------------------------------------------------------------

LangParser::UpdateStatementContext::UpdateStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::UpdateStatementContext::getRuleIndex() const {
  return LangParser::RuleUpdateStatement;
}

void LangParser::UpdateStatementContext::copyFrom(UpdateStatementContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- UpdateAssignStmtContext ------------------------------------------------------------------

LangParser::LvalueContext* LangParser::UpdateAssignStmtContext::lvalue() {
  return getRuleContext<LangParser::LvalueContext>(0);
}

LangParser::ExpressionContext* LangParser::UpdateAssignStmtContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

tree::TerminalNode* LangParser::UpdateAssignStmtContext::ADD_ASSIGN() {
  return getToken(LangParser::ADD_ASSIGN, 0);
}

tree::TerminalNode* LangParser::UpdateAssignStmtContext::SUB_ASSIGN() {
  return getToken(LangParser::SUB_ASSIGN, 0);
}

tree::TerminalNode* LangParser::UpdateAssignStmtContext::MUL_ASSIGN() {
  return getToken(LangParser::MUL_ASSIGN, 0);
}

tree::TerminalNode* LangParser::UpdateAssignStmtContext::DIV_ASSIGN() {
  return getToken(LangParser::DIV_ASSIGN, 0);
}

tree::TerminalNode* LangParser::UpdateAssignStmtContext::MOD_ASSIGN() {
  return getToken(LangParser::MOD_ASSIGN, 0);
}

tree::TerminalNode* LangParser::UpdateAssignStmtContext::CONCAT_ASSIGN() {
  return getToken(LangParser::CONCAT_ASSIGN, 0);
}

LangParser::UpdateAssignStmtContext::UpdateAssignStmtContext(UpdateStatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::UpdateAssignStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitUpdateAssignStmt(this);
  else
    return visitor->visitChildren(this);
}
LangParser::UpdateStatementContext* LangParser::updateStatement() {
  UpdateStatementContext *_localctx = _tracker.createInstance<UpdateStatementContext>(_ctx, getState());
  enterRule(_localctx, 10, LangParser::RuleUpdateStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::UpdateAssignStmtContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(196);
    lvalue();
    setState(197);
    antlrcpp::downCast<UpdateAssignStmtContext *>(_localctx)->op = _input->LT(1);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 138538465099776) != 0))) {
      antlrcpp::downCast<UpdateAssignStmtContext *>(_localctx)->op = _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(198);
    expression();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AssignStatementContext ------------------------------------------------------------------

LangParser::AssignStatementContext::AssignStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::AssignStatementContext::getRuleIndex() const {
  return LangParser::RuleAssignStatement;
}

void LangParser::AssignStatementContext::copyFrom(AssignStatementContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- NormalAssignStmtContext ------------------------------------------------------------------

std::vector<LangParser::LvalueContext *> LangParser::NormalAssignStmtContext::lvalue() {
  return getRuleContexts<LangParser::LvalueContext>();
}

LangParser::LvalueContext* LangParser::NormalAssignStmtContext::lvalue(size_t i) {
  return getRuleContext<LangParser::LvalueContext>(i);
}

tree::TerminalNode* LangParser::NormalAssignStmtContext::ASSIGN() {
  return getToken(LangParser::ASSIGN, 0);
}

std::vector<LangParser::ExpressionContext *> LangParser::NormalAssignStmtContext::expression() {
  return getRuleContexts<LangParser::ExpressionContext>();
}

LangParser::ExpressionContext* LangParser::NormalAssignStmtContext::expression(size_t i) {
  return getRuleContext<LangParser::ExpressionContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::NormalAssignStmtContext::COMMA() {
  return getTokens(LangParser::COMMA);
}

tree::TerminalNode* LangParser::NormalAssignStmtContext::COMMA(size_t i) {
  return getToken(LangParser::COMMA, i);
}

LangParser::NormalAssignStmtContext::NormalAssignStmtContext(AssignStatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::NormalAssignStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitNormalAssignStmt(this);
  else
    return visitor->visitChildren(this);
}
LangParser::AssignStatementContext* LangParser::assignStatement() {
  AssignStatementContext *_localctx = _tracker.createInstance<AssignStatementContext>(_ctx, getState());
  enterRule(_localctx, 12, LangParser::RuleAssignStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    _localctx = _tracker.createInstance<LangParser::NormalAssignStmtContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(200);
    lvalue();
    setState(205);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::COMMA) {
      setState(201);
      match(LangParser::COMMA);
      setState(202);
      lvalue();
      setState(207);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(208);
    match(LangParser::ASSIGN);
    setState(209);
    expression();
    setState(214);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 9, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(210);
        match(LangParser::COMMA);
        setState(211);
        expression(); 
      }
      setState(216);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 9, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LvalueContext ------------------------------------------------------------------

LangParser::LvalueContext::LvalueContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::LvalueContext::getRuleIndex() const {
  return LangParser::RuleLvalue;
}

void LangParser::LvalueContext::copyFrom(LvalueContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- LvalueBaseContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::LvalueBaseContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

std::vector<LangParser::LvalueSuffixContext *> LangParser::LvalueBaseContext::lvalueSuffix() {
  return getRuleContexts<LangParser::LvalueSuffixContext>();
}

LangParser::LvalueSuffixContext* LangParser::LvalueBaseContext::lvalueSuffix(size_t i) {
  return getRuleContext<LangParser::LvalueSuffixContext>(i);
}

LangParser::LvalueBaseContext::LvalueBaseContext(LvalueContext *ctx) { copyFrom(ctx); }


std::any LangParser::LvalueBaseContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitLvalueBase(this);
  else
    return visitor->visitChildren(this);
}
LangParser::LvalueContext* LangParser::lvalue() {
  LvalueContext *_localctx = _tracker.createInstance<LvalueContext>(_ctx, getState());
  enterRule(_localctx, 14, LangParser::RuleLvalue);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::LvalueBaseContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(217);
    match(LangParser::IDENTIFIER);
    setState(221);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::OSB

    || _la == LangParser::DOT) {
      setState(218);
      lvalueSuffix();
      setState(223);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LvalueSuffixContext ------------------------------------------------------------------

LangParser::LvalueSuffixContext::LvalueSuffixContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::LvalueSuffixContext::getRuleIndex() const {
  return LangParser::RuleLvalueSuffix;
}

void LangParser::LvalueSuffixContext::copyFrom(LvalueSuffixContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- LvalueIndexContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::LvalueIndexContext::OSB() {
  return getToken(LangParser::OSB, 0);
}

LangParser::ExpressionContext* LangParser::LvalueIndexContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

tree::TerminalNode* LangParser::LvalueIndexContext::CSB() {
  return getToken(LangParser::CSB, 0);
}

LangParser::LvalueIndexContext::LvalueIndexContext(LvalueSuffixContext *ctx) { copyFrom(ctx); }


std::any LangParser::LvalueIndexContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitLvalueIndex(this);
  else
    return visitor->visitChildren(this);
}
//----------------- LvalueMemberContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::LvalueMemberContext::DOT() {
  return getToken(LangParser::DOT, 0);
}

tree::TerminalNode* LangParser::LvalueMemberContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

LangParser::LvalueMemberContext::LvalueMemberContext(LvalueSuffixContext *ctx) { copyFrom(ctx); }


std::any LangParser::LvalueMemberContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitLvalueMember(this);
  else
    return visitor->visitChildren(this);
}
LangParser::LvalueSuffixContext* LangParser::lvalueSuffix() {
  LvalueSuffixContext *_localctx = _tracker.createInstance<LvalueSuffixContext>(_ctx, getState());
  enterRule(_localctx, 16, LangParser::RuleLvalueSuffix);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(230);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::OSB: {
        _localctx = _tracker.createInstance<LangParser::LvalueIndexContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(224);
        match(LangParser::OSB);
        setState(225);
        expression();
        setState(226);
        match(LangParser::CSB);
        break;
      }

      case LangParser::DOT: {
        _localctx = _tracker.createInstance<LangParser::LvalueMemberContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(228);
        match(LangParser::DOT);
        setState(229);
        match(LangParser::IDENTIFIER);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DeclarationContext ------------------------------------------------------------------

LangParser::DeclarationContext::DeclarationContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

LangParser::VariableDeclarationContext* LangParser::DeclarationContext::variableDeclaration() {
  return getRuleContext<LangParser::VariableDeclarationContext>(0);
}

tree::TerminalNode* LangParser::DeclarationContext::SEMICOLON() {
  return getToken(LangParser::SEMICOLON, 0);
}

LangParser::FunctionDeclarationContext* LangParser::DeclarationContext::functionDeclaration() {
  return getRuleContext<LangParser::FunctionDeclarationContext>(0);
}

LangParser::ClassDeclarationContext* LangParser::DeclarationContext::classDeclaration() {
  return getRuleContext<LangParser::ClassDeclarationContext>(0);
}

tree::TerminalNode* LangParser::DeclarationContext::EXPORT() {
  return getToken(LangParser::EXPORT, 0);
}


size_t LangParser::DeclarationContext::getRuleIndex() const {
  return LangParser::RuleDeclaration;
}


std::any LangParser::DeclarationContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitDeclaration(this);
  else
    return visitor->visitChildren(this);
}

LangParser::DeclarationContext* LangParser::declaration() {
  DeclarationContext *_localctx = _tracker.createInstance<DeclarationContext>(_ctx, getState());
  enterRule(_localctx, 18, LangParser::RuleDeclaration);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(233);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::EXPORT) {
      setState(232);
      match(LangParser::EXPORT);
    }
    setState(240);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 13, _ctx)) {
    case 1: {
      setState(235);
      variableDeclaration();
      setState(236);
      match(LangParser::SEMICOLON);
      break;
    }

    case 2: {
      setState(238);
      functionDeclaration();
      break;
    }

    case 3: {
      setState(239);
      classDeclaration();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VariableDeclarationContext ------------------------------------------------------------------

LangParser::VariableDeclarationContext::VariableDeclarationContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::VariableDeclarationContext::getRuleIndex() const {
  return LangParser::RuleVariableDeclaration;
}

void LangParser::VariableDeclarationContext::copyFrom(VariableDeclarationContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- MutiVariableDeclarationDefContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::MutiVariableDeclarationDefContext::MUTIVAR() {
  return getToken(LangParser::MUTIVAR, 0);
}

std::vector<tree::TerminalNode *> LangParser::MutiVariableDeclarationDefContext::IDENTIFIER() {
  return getTokens(LangParser::IDENTIFIER);
}

tree::TerminalNode* LangParser::MutiVariableDeclarationDefContext::IDENTIFIER(size_t i) {
  return getToken(LangParser::IDENTIFIER, i);
}

std::vector<tree::TerminalNode *> LangParser::MutiVariableDeclarationDefContext::GLOBAL() {
  return getTokens(LangParser::GLOBAL);
}

tree::TerminalNode* LangParser::MutiVariableDeclarationDefContext::GLOBAL(size_t i) {
  return getToken(LangParser::GLOBAL, i);
}

std::vector<tree::TerminalNode *> LangParser::MutiVariableDeclarationDefContext::CONST() {
  return getTokens(LangParser::CONST);
}

tree::TerminalNode* LangParser::MutiVariableDeclarationDefContext::CONST(size_t i) {
  return getToken(LangParser::CONST, i);
}

std::vector<tree::TerminalNode *> LangParser::MutiVariableDeclarationDefContext::COMMA() {
  return getTokens(LangParser::COMMA);
}

tree::TerminalNode* LangParser::MutiVariableDeclarationDefContext::COMMA(size_t i) {
  return getToken(LangParser::COMMA, i);
}

tree::TerminalNode* LangParser::MutiVariableDeclarationDefContext::ASSIGN() {
  return getToken(LangParser::ASSIGN, 0);
}

LangParser::ExpressionContext* LangParser::MutiVariableDeclarationDefContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

LangParser::MutiVariableDeclarationDefContext::MutiVariableDeclarationDefContext(VariableDeclarationContext *ctx) { copyFrom(ctx); }


std::any LangParser::MutiVariableDeclarationDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMutiVariableDeclarationDef(this);
  else
    return visitor->visitChildren(this);
}
//----------------- VariableDeclarationDefContext ------------------------------------------------------------------

LangParser::Declaration_itemContext* LangParser::VariableDeclarationDefContext::declaration_item() {
  return getRuleContext<LangParser::Declaration_itemContext>(0);
}

tree::TerminalNode* LangParser::VariableDeclarationDefContext::GLOBAL() {
  return getToken(LangParser::GLOBAL, 0);
}

tree::TerminalNode* LangParser::VariableDeclarationDefContext::CONST() {
  return getToken(LangParser::CONST, 0);
}

tree::TerminalNode* LangParser::VariableDeclarationDefContext::ASSIGN() {
  return getToken(LangParser::ASSIGN, 0);
}

LangParser::ExpressionContext* LangParser::VariableDeclarationDefContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

LangParser::VariableDeclarationDefContext::VariableDeclarationDefContext(VariableDeclarationContext *ctx) { copyFrom(ctx); }


std::any LangParser::VariableDeclarationDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitVariableDeclarationDef(this);
  else
    return visitor->visitChildren(this);
}
LangParser::VariableDeclarationContext* LangParser::variableDeclaration() {
  VariableDeclarationContext *_localctx = _tracker.createInstance<VariableDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 20, LangParser::RuleVariableDeclaration);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(278);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::INT:
      case LangParser::FLOAT:
      case LangParser::NUMBER:
      case LangParser::STRING:
      case LangParser::BOOL:
      case LangParser::ANY:
      case LangParser::VOID:
      case LangParser::NULL_:
      case LangParser::LIST:
      case LangParser::MAP:
      case LangParser::FUNCTION:
      case LangParser::COROUTINE:
      case LangParser::CONST:
      case LangParser::AUTO:
      case LangParser::GLOBAL:
      case LangParser::IDENTIFIER: {
        _localctx = _tracker.createInstance<LangParser::VariableDeclarationDefContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(243);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::GLOBAL) {
          setState(242);
          match(LangParser::GLOBAL);
        }
        setState(246);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::CONST) {
          setState(245);
          match(LangParser::CONST);
        }
        setState(248);
        declaration_item();
        setState(251);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::ASSIGN) {
          setState(249);
          match(LangParser::ASSIGN);
          setState(250);
          expression();
        }
        break;
      }

      case LangParser::MUTIVAR: {
        _localctx = _tracker.createInstance<LangParser::MutiVariableDeclarationDefContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(253);
        match(LangParser::MUTIVAR);
        setState(255);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::GLOBAL) {
          setState(254);
          match(LangParser::GLOBAL);
        }
        setState(258);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::CONST) {
          setState(257);
          match(LangParser::CONST);
        }
        setState(260);
        match(LangParser::IDENTIFIER);
        setState(271);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == LangParser::COMMA) {
          setState(261);
          match(LangParser::COMMA);
          setState(263);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == LangParser::GLOBAL) {
            setState(262);
            match(LangParser::GLOBAL);
          }
          setState(266);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == LangParser::CONST) {
            setState(265);
            match(LangParser::CONST);
          }
          setState(268);
          match(LangParser::IDENTIFIER);
          setState(273);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        setState(276);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::ASSIGN) {
          setState(274);
          match(LangParser::ASSIGN);
          setState(275);
          expression();
        }
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Declaration_itemContext ------------------------------------------------------------------

LangParser::Declaration_itemContext::Declaration_itemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::Declaration_itemContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

LangParser::TypeContext* LangParser::Declaration_itemContext::type() {
  return getRuleContext<LangParser::TypeContext>(0);
}

tree::TerminalNode* LangParser::Declaration_itemContext::AUTO() {
  return getToken(LangParser::AUTO, 0);
}


size_t LangParser::Declaration_itemContext::getRuleIndex() const {
  return LangParser::RuleDeclaration_item;
}


std::any LangParser::Declaration_itemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitDeclaration_item(this);
  else
    return visitor->visitChildren(this);
}

LangParser::Declaration_itemContext* LangParser::declaration_item() {
  Declaration_itemContext *_localctx = _tracker.createInstance<Declaration_itemContext>(_ctx, getState());
  enterRule(_localctx, 22, LangParser::RuleDeclaration_item);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(282);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::INT:
      case LangParser::FLOAT:
      case LangParser::NUMBER:
      case LangParser::STRING:
      case LangParser::BOOL:
      case LangParser::ANY:
      case LangParser::VOID:
      case LangParser::NULL_:
      case LangParser::LIST:
      case LangParser::MAP:
      case LangParser::FUNCTION:
      case LangParser::COROUTINE:
      case LangParser::IDENTIFIER: {
        setState(280);
        type();
        break;
      }

      case LangParser::AUTO: {
        setState(281);
        match(LangParser::AUTO);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(284);
    match(LangParser::IDENTIFIER);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FunctionDeclarationContext ------------------------------------------------------------------

LangParser::FunctionDeclarationContext::FunctionDeclarationContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::FunctionDeclarationContext::getRuleIndex() const {
  return LangParser::RuleFunctionDeclaration;
}

void LangParser::FunctionDeclarationContext::copyFrom(FunctionDeclarationContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- FunctionDeclarationDefContext ------------------------------------------------------------------

LangParser::TypeContext* LangParser::FunctionDeclarationDefContext::type() {
  return getRuleContext<LangParser::TypeContext>(0);
}

LangParser::QualifiedIdentifierContext* LangParser::FunctionDeclarationDefContext::qualifiedIdentifier() {
  return getRuleContext<LangParser::QualifiedIdentifierContext>(0);
}

tree::TerminalNode* LangParser::FunctionDeclarationDefContext::OP() {
  return getToken(LangParser::OP, 0);
}

tree::TerminalNode* LangParser::FunctionDeclarationDefContext::CP() {
  return getToken(LangParser::CP, 0);
}

LangParser::BlockStatementContext* LangParser::FunctionDeclarationDefContext::blockStatement() {
  return getRuleContext<LangParser::BlockStatementContext>(0);
}

tree::TerminalNode* LangParser::FunctionDeclarationDefContext::GLOBAL() {
  return getToken(LangParser::GLOBAL, 0);
}

LangParser::ParameterListContext* LangParser::FunctionDeclarationDefContext::parameterList() {
  return getRuleContext<LangParser::ParameterListContext>(0);
}

LangParser::FunctionDeclarationDefContext::FunctionDeclarationDefContext(FunctionDeclarationContext *ctx) { copyFrom(ctx); }


std::any LangParser::FunctionDeclarationDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitFunctionDeclarationDef(this);
  else
    return visitor->visitChildren(this);
}
//----------------- MultiReturnFunctionDeclarationDefContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::MultiReturnFunctionDeclarationDefContext::MUTIVAR() {
  return getToken(LangParser::MUTIVAR, 0);
}

LangParser::QualifiedIdentifierContext* LangParser::MultiReturnFunctionDeclarationDefContext::qualifiedIdentifier() {
  return getRuleContext<LangParser::QualifiedIdentifierContext>(0);
}

tree::TerminalNode* LangParser::MultiReturnFunctionDeclarationDefContext::OP() {
  return getToken(LangParser::OP, 0);
}

tree::TerminalNode* LangParser::MultiReturnFunctionDeclarationDefContext::CP() {
  return getToken(LangParser::CP, 0);
}

LangParser::BlockStatementContext* LangParser::MultiReturnFunctionDeclarationDefContext::blockStatement() {
  return getRuleContext<LangParser::BlockStatementContext>(0);
}

tree::TerminalNode* LangParser::MultiReturnFunctionDeclarationDefContext::GLOBAL() {
  return getToken(LangParser::GLOBAL, 0);
}

LangParser::ParameterListContext* LangParser::MultiReturnFunctionDeclarationDefContext::parameterList() {
  return getRuleContext<LangParser::ParameterListContext>(0);
}

LangParser::MultiReturnFunctionDeclarationDefContext::MultiReturnFunctionDeclarationDefContext(FunctionDeclarationContext *ctx) { copyFrom(ctx); }


std::any LangParser::MultiReturnFunctionDeclarationDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMultiReturnFunctionDeclarationDef(this);
  else
    return visitor->visitChildren(this);
}
LangParser::FunctionDeclarationContext* LangParser::functionDeclaration() {
  FunctionDeclarationContext *_localctx = _tracker.createInstance<FunctionDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 24, LangParser::RuleFunctionDeclaration);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(310);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::FunctionDeclarationDefContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(287);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::GLOBAL) {
        setState(286);
        match(LangParser::GLOBAL);
      }
      setState(289);
      type();
      setState(290);
      qualifiedIdentifier();
      setState(291);
      match(LangParser::OP);
      setState(293);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8190) != 0) || _la == LangParser::DDD

      || _la == LangParser::IDENTIFIER) {
        setState(292);
        parameterList();
      }
      setState(295);
      match(LangParser::CP);
      setState(296);
      blockStatement();
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<LangParser::MultiReturnFunctionDeclarationDefContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(299);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::GLOBAL) {
        setState(298);
        match(LangParser::GLOBAL);
      }
      setState(301);
      match(LangParser::MUTIVAR);
      setState(302);
      qualifiedIdentifier();
      setState(303);
      match(LangParser::OP);
      setState(305);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8190) != 0) || _la == LangParser::DDD

      || _la == LangParser::IDENTIFIER) {
        setState(304);
        parameterList();
      }
      setState(307);
      match(LangParser::CP);
      setState(308);
      blockStatement();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ClassDeclarationContext ------------------------------------------------------------------

LangParser::ClassDeclarationContext::ClassDeclarationContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::ClassDeclarationContext::getRuleIndex() const {
  return LangParser::RuleClassDeclaration;
}

void LangParser::ClassDeclarationContext::copyFrom(ClassDeclarationContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ClassDeclarationDefContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::ClassDeclarationDefContext::CLASS() {
  return getToken(LangParser::CLASS, 0);
}

tree::TerminalNode* LangParser::ClassDeclarationDefContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

tree::TerminalNode* LangParser::ClassDeclarationDefContext::OCB() {
  return getToken(LangParser::OCB, 0);
}

tree::TerminalNode* LangParser::ClassDeclarationDefContext::CCB() {
  return getToken(LangParser::CCB, 0);
}

std::vector<LangParser::ClassMemberContext *> LangParser::ClassDeclarationDefContext::classMember() {
  return getRuleContexts<LangParser::ClassMemberContext>();
}

LangParser::ClassMemberContext* LangParser::ClassDeclarationDefContext::classMember(size_t i) {
  return getRuleContext<LangParser::ClassMemberContext>(i);
}

LangParser::ClassDeclarationDefContext::ClassDeclarationDefContext(ClassDeclarationContext *ctx) { copyFrom(ctx); }


std::any LangParser::ClassDeclarationDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitClassDeclarationDef(this);
  else
    return visitor->visitChildren(this);
}
LangParser::ClassDeclarationContext* LangParser::classDeclaration() {
  ClassDeclarationContext *_localctx = _tracker.createInstance<ClassDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 26, LangParser::RuleClassDeclaration);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::ClassDeclarationDefContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(312);
    match(LangParser::CLASS);
    setState(313);
    match(LangParser::IDENTIFIER);
    setState(314);
    match(LangParser::OCB);
    setState(318);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 92291070) != 0) || _la == LangParser::SEMICOLON

    || _la == LangParser::IDENTIFIER) {
      setState(315);
      classMember();
      setState(320);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(321);
    match(LangParser::CCB);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ClassMemberContext ------------------------------------------------------------------

LangParser::ClassMemberContext::ClassMemberContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::ClassMemberContext::getRuleIndex() const {
  return LangParser::RuleClassMember;
}

void LangParser::ClassMemberContext::copyFrom(ClassMemberContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ClassMethodMemberContext ------------------------------------------------------------------

LangParser::TypeContext* LangParser::ClassMethodMemberContext::type() {
  return getRuleContext<LangParser::TypeContext>(0);
}

tree::TerminalNode* LangParser::ClassMethodMemberContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

tree::TerminalNode* LangParser::ClassMethodMemberContext::OP() {
  return getToken(LangParser::OP, 0);
}

tree::TerminalNode* LangParser::ClassMethodMemberContext::CP() {
  return getToken(LangParser::CP, 0);
}

LangParser::BlockStatementContext* LangParser::ClassMethodMemberContext::blockStatement() {
  return getRuleContext<LangParser::BlockStatementContext>(0);
}

tree::TerminalNode* LangParser::ClassMethodMemberContext::STATIC() {
  return getToken(LangParser::STATIC, 0);
}

LangParser::ParameterListContext* LangParser::ClassMethodMemberContext::parameterList() {
  return getRuleContext<LangParser::ParameterListContext>(0);
}

LangParser::ClassMethodMemberContext::ClassMethodMemberContext(ClassMemberContext *ctx) { copyFrom(ctx); }


std::any LangParser::ClassMethodMemberContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitClassMethodMember(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ClassFieldMemberContext ------------------------------------------------------------------

LangParser::Declaration_itemContext* LangParser::ClassFieldMemberContext::declaration_item() {
  return getRuleContext<LangParser::Declaration_itemContext>(0);
}

tree::TerminalNode* LangParser::ClassFieldMemberContext::STATIC() {
  return getToken(LangParser::STATIC, 0);
}

tree::TerminalNode* LangParser::ClassFieldMemberContext::CONST() {
  return getToken(LangParser::CONST, 0);
}

tree::TerminalNode* LangParser::ClassFieldMemberContext::ASSIGN() {
  return getToken(LangParser::ASSIGN, 0);
}

LangParser::ExpressionContext* LangParser::ClassFieldMemberContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

LangParser::ClassFieldMemberContext::ClassFieldMemberContext(ClassMemberContext *ctx) { copyFrom(ctx); }


std::any LangParser::ClassFieldMemberContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitClassFieldMember(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ClassEmptyMemberContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::ClassEmptyMemberContext::SEMICOLON() {
  return getToken(LangParser::SEMICOLON, 0);
}

LangParser::ClassEmptyMemberContext::ClassEmptyMemberContext(ClassMemberContext *ctx) { copyFrom(ctx); }


std::any LangParser::ClassEmptyMemberContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitClassEmptyMember(this);
  else
    return visitor->visitChildren(this);
}
//----------------- MultiReturnClassMethodMemberContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::MultiReturnClassMethodMemberContext::MUTIVAR() {
  return getToken(LangParser::MUTIVAR, 0);
}

tree::TerminalNode* LangParser::MultiReturnClassMethodMemberContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

tree::TerminalNode* LangParser::MultiReturnClassMethodMemberContext::OP() {
  return getToken(LangParser::OP, 0);
}

tree::TerminalNode* LangParser::MultiReturnClassMethodMemberContext::CP() {
  return getToken(LangParser::CP, 0);
}

LangParser::BlockStatementContext* LangParser::MultiReturnClassMethodMemberContext::blockStatement() {
  return getRuleContext<LangParser::BlockStatementContext>(0);
}

tree::TerminalNode* LangParser::MultiReturnClassMethodMemberContext::STATIC() {
  return getToken(LangParser::STATIC, 0);
}

LangParser::ParameterListContext* LangParser::MultiReturnClassMethodMemberContext::parameterList() {
  return getRuleContext<LangParser::ParameterListContext>(0);
}

LangParser::MultiReturnClassMethodMemberContext::MultiReturnClassMethodMemberContext(ClassMemberContext *ctx) { copyFrom(ctx); }


std::any LangParser::MultiReturnClassMethodMemberContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMultiReturnClassMethodMember(this);
  else
    return visitor->visitChildren(this);
}
LangParser::ClassMemberContext* LangParser::classMember() {
  ClassMemberContext *_localctx = _tracker.createInstance<ClassMemberContext>(_ctx, getState());
  enterRule(_localctx, 28, LangParser::RuleClassMember);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(358);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 38, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::ClassFieldMemberContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(324);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::STATIC) {
        setState(323);
        match(LangParser::STATIC);
      }
      setState(327);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::CONST) {
        setState(326);
        match(LangParser::CONST);
      }
      setState(329);
      declaration_item();
      setState(332);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::ASSIGN) {
        setState(330);
        match(LangParser::ASSIGN);
        setState(331);
        expression();
      }
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<LangParser::ClassMethodMemberContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(335);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::STATIC) {
        setState(334);
        match(LangParser::STATIC);
      }
      setState(337);
      type();
      setState(338);
      match(LangParser::IDENTIFIER);
      setState(339);
      match(LangParser::OP);
      setState(341);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8190) != 0) || _la == LangParser::DDD

      || _la == LangParser::IDENTIFIER) {
        setState(340);
        parameterList();
      }
      setState(343);
      match(LangParser::CP);
      setState(344);
      blockStatement();
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<LangParser::MultiReturnClassMethodMemberContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(347);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::STATIC) {
        setState(346);
        match(LangParser::STATIC);
      }
      setState(349);
      match(LangParser::MUTIVAR);
      setState(350);
      match(LangParser::IDENTIFIER);
      setState(351);
      match(LangParser::OP);
      setState(353);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8190) != 0) || _la == LangParser::DDD

      || _la == LangParser::IDENTIFIER) {
        setState(352);
        parameterList();
      }
      setState(355);
      match(LangParser::CP);
      setState(356);
      blockStatement();
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<LangParser::ClassEmptyMemberContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(357);
      match(LangParser::SEMICOLON);
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeContext ------------------------------------------------------------------

LangParser::TypeContext::TypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::TypeContext::getRuleIndex() const {
  return LangParser::RuleType;
}

void LangParser::TypeContext::copyFrom(TypeContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- TypeMapContext ------------------------------------------------------------------

LangParser::MapTypeContext* LangParser::TypeMapContext::mapType() {
  return getRuleContext<LangParser::MapTypeContext>(0);
}

LangParser::TypeMapContext::TypeMapContext(TypeContext *ctx) { copyFrom(ctx); }


std::any LangParser::TypeMapContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitTypeMap(this);
  else
    return visitor->visitChildren(this);
}
//----------------- TypeAnyContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::TypeAnyContext::ANY() {
  return getToken(LangParser::ANY, 0);
}

LangParser::TypeAnyContext::TypeAnyContext(TypeContext *ctx) { copyFrom(ctx); }


std::any LangParser::TypeAnyContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitTypeAny(this);
  else
    return visitor->visitChildren(this);
}
//----------------- TypeListTypeContext ------------------------------------------------------------------

LangParser::ListTypeContext* LangParser::TypeListTypeContext::listType() {
  return getRuleContext<LangParser::ListTypeContext>(0);
}

LangParser::TypeListTypeContext::TypeListTypeContext(TypeContext *ctx) { copyFrom(ctx); }


std::any LangParser::TypeListTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitTypeListType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- TypeQualifiedIdentifierContext ------------------------------------------------------------------

LangParser::QualifiedIdentifierContext* LangParser::TypeQualifiedIdentifierContext::qualifiedIdentifier() {
  return getRuleContext<LangParser::QualifiedIdentifierContext>(0);
}

LangParser::TypeQualifiedIdentifierContext::TypeQualifiedIdentifierContext(TypeContext *ctx) { copyFrom(ctx); }


std::any LangParser::TypeQualifiedIdentifierContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitTypeQualifiedIdentifier(this);
  else
    return visitor->visitChildren(this);
}
//----------------- TypePrimitiveContext ------------------------------------------------------------------

LangParser::PrimitiveTypeContext* LangParser::TypePrimitiveContext::primitiveType() {
  return getRuleContext<LangParser::PrimitiveTypeContext>(0);
}

LangParser::TypePrimitiveContext::TypePrimitiveContext(TypeContext *ctx) { copyFrom(ctx); }


std::any LangParser::TypePrimitiveContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitTypePrimitive(this);
  else
    return visitor->visitChildren(this);
}
LangParser::TypeContext* LangParser::type() {
  TypeContext *_localctx = _tracker.createInstance<TypeContext>(_ctx, getState());
  enterRule(_localctx, 30, LangParser::RuleType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(365);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::INT:
      case LangParser::FLOAT:
      case LangParser::NUMBER:
      case LangParser::STRING:
      case LangParser::BOOL:
      case LangParser::VOID:
      case LangParser::NULL_:
      case LangParser::FUNCTION:
      case LangParser::COROUTINE: {
        _localctx = _tracker.createInstance<LangParser::TypePrimitiveContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(360);
        primitiveType();
        break;
      }

      case LangParser::LIST: {
        _localctx = _tracker.createInstance<LangParser::TypeListTypeContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(361);
        listType();
        break;
      }

      case LangParser::MAP: {
        _localctx = _tracker.createInstance<LangParser::TypeMapContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(362);
        mapType();
        break;
      }

      case LangParser::ANY: {
        _localctx = _tracker.createInstance<LangParser::TypeAnyContext>(_localctx);
        enterOuterAlt(_localctx, 4);
        setState(363);
        match(LangParser::ANY);
        break;
      }

      case LangParser::IDENTIFIER: {
        _localctx = _tracker.createInstance<LangParser::TypeQualifiedIdentifierContext>(_localctx);
        enterOuterAlt(_localctx, 5);
        setState(364);
        qualifiedIdentifier();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- QualifiedIdentifierContext ------------------------------------------------------------------

LangParser::QualifiedIdentifierContext::QualifiedIdentifierContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> LangParser::QualifiedIdentifierContext::IDENTIFIER() {
  return getTokens(LangParser::IDENTIFIER);
}

tree::TerminalNode* LangParser::QualifiedIdentifierContext::IDENTIFIER(size_t i) {
  return getToken(LangParser::IDENTIFIER, i);
}

std::vector<tree::TerminalNode *> LangParser::QualifiedIdentifierContext::DOT() {
  return getTokens(LangParser::DOT);
}

tree::TerminalNode* LangParser::QualifiedIdentifierContext::DOT(size_t i) {
  return getToken(LangParser::DOT, i);
}


size_t LangParser::QualifiedIdentifierContext::getRuleIndex() const {
  return LangParser::RuleQualifiedIdentifier;
}


std::any LangParser::QualifiedIdentifierContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitQualifiedIdentifier(this);
  else
    return visitor->visitChildren(this);
}

LangParser::QualifiedIdentifierContext* LangParser::qualifiedIdentifier() {
  QualifiedIdentifierContext *_localctx = _tracker.createInstance<QualifiedIdentifierContext>(_ctx, getState());
  enterRule(_localctx, 32, LangParser::RuleQualifiedIdentifier);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(367);
    match(LangParser::IDENTIFIER);
    setState(372);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::DOT) {
      setState(368);
      match(LangParser::DOT);
      setState(369);
      match(LangParser::IDENTIFIER);
      setState(374);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimitiveTypeContext ------------------------------------------------------------------

LangParser::PrimitiveTypeContext::PrimitiveTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::PrimitiveTypeContext::INT() {
  return getToken(LangParser::INT, 0);
}

tree::TerminalNode* LangParser::PrimitiveTypeContext::FLOAT() {
  return getToken(LangParser::FLOAT, 0);
}

tree::TerminalNode* LangParser::PrimitiveTypeContext::NUMBER() {
  return getToken(LangParser::NUMBER, 0);
}

tree::TerminalNode* LangParser::PrimitiveTypeContext::STRING() {
  return getToken(LangParser::STRING, 0);
}

tree::TerminalNode* LangParser::PrimitiveTypeContext::BOOL() {
  return getToken(LangParser::BOOL, 0);
}

tree::TerminalNode* LangParser::PrimitiveTypeContext::VOID() {
  return getToken(LangParser::VOID, 0);
}

tree::TerminalNode* LangParser::PrimitiveTypeContext::NULL_() {
  return getToken(LangParser::NULL_, 0);
}

tree::TerminalNode* LangParser::PrimitiveTypeContext::COROUTINE() {
  return getToken(LangParser::COROUTINE, 0);
}

tree::TerminalNode* LangParser::PrimitiveTypeContext::FUNCTION() {
  return getToken(LangParser::FUNCTION, 0);
}


size_t LangParser::PrimitiveTypeContext::getRuleIndex() const {
  return LangParser::RulePrimitiveType;
}


std::any LangParser::PrimitiveTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPrimitiveType(this);
  else
    return visitor->visitChildren(this);
}

LangParser::PrimitiveTypeContext* LangParser::primitiveType() {
  PrimitiveTypeContext *_localctx = _tracker.createInstance<PrimitiveTypeContext>(_ctx, getState());
  enterRule(_localctx, 34, LangParser::RulePrimitiveType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(375);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 6590) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ListTypeContext ------------------------------------------------------------------

LangParser::ListTypeContext::ListTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::ListTypeContext::LIST() {
  return getToken(LangParser::LIST, 0);
}

tree::TerminalNode* LangParser::ListTypeContext::LT() {
  return getToken(LangParser::LT, 0);
}

LangParser::TypeContext* LangParser::ListTypeContext::type() {
  return getRuleContext<LangParser::TypeContext>(0);
}

tree::TerminalNode* LangParser::ListTypeContext::GT() {
  return getToken(LangParser::GT, 0);
}


size_t LangParser::ListTypeContext::getRuleIndex() const {
  return LangParser::RuleListType;
}


std::any LangParser::ListTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitListType(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ListTypeContext* LangParser::listType() {
  ListTypeContext *_localctx = _tracker.createInstance<ListTypeContext>(_ctx, getState());
  enterRule(_localctx, 36, LangParser::RuleListType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(377);
    match(LangParser::LIST);
    setState(382);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::LT) {
      setState(378);
      match(LangParser::LT);
      setState(379);
      type();
      setState(380);
      match(LangParser::GT);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MapTypeContext ------------------------------------------------------------------

LangParser::MapTypeContext::MapTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::MapTypeContext::MAP() {
  return getToken(LangParser::MAP, 0);
}

tree::TerminalNode* LangParser::MapTypeContext::LT() {
  return getToken(LangParser::LT, 0);
}

std::vector<LangParser::TypeContext *> LangParser::MapTypeContext::type() {
  return getRuleContexts<LangParser::TypeContext>();
}

LangParser::TypeContext* LangParser::MapTypeContext::type(size_t i) {
  return getRuleContext<LangParser::TypeContext>(i);
}

tree::TerminalNode* LangParser::MapTypeContext::COMMA() {
  return getToken(LangParser::COMMA, 0);
}

tree::TerminalNode* LangParser::MapTypeContext::GT() {
  return getToken(LangParser::GT, 0);
}


size_t LangParser::MapTypeContext::getRuleIndex() const {
  return LangParser::RuleMapType;
}


std::any LangParser::MapTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMapType(this);
  else
    return visitor->visitChildren(this);
}

LangParser::MapTypeContext* LangParser::mapType() {
  MapTypeContext *_localctx = _tracker.createInstance<MapTypeContext>(_ctx, getState());
  enterRule(_localctx, 38, LangParser::RuleMapType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(384);
    match(LangParser::MAP);
    setState(391);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::LT) {
      setState(385);
      match(LangParser::LT);
      setState(386);
      type();
      setState(387);
      match(LangParser::COMMA);
      setState(388);
      type();
      setState(389);
      match(LangParser::GT);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExpressionContext ------------------------------------------------------------------

LangParser::ExpressionContext::ExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

LangParser::LogicalOrExpContext* LangParser::ExpressionContext::logicalOrExp() {
  return getRuleContext<LangParser::LogicalOrExpContext>(0);
}


size_t LangParser::ExpressionContext::getRuleIndex() const {
  return LangParser::RuleExpression;
}


std::any LangParser::ExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitExpression(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ExpressionContext* LangParser::expression() {
  ExpressionContext *_localctx = _tracker.createInstance<ExpressionContext>(_ctx, getState());
  enterRule(_localctx, 40, LangParser::RuleExpression);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(393);
    logicalOrExp();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExpressionListContext ------------------------------------------------------------------

LangParser::ExpressionListContext::ExpressionListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<LangParser::ExpressionContext *> LangParser::ExpressionListContext::expression() {
  return getRuleContexts<LangParser::ExpressionContext>();
}

LangParser::ExpressionContext* LangParser::ExpressionListContext::expression(size_t i) {
  return getRuleContext<LangParser::ExpressionContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::ExpressionListContext::COMMA() {
  return getTokens(LangParser::COMMA);
}

tree::TerminalNode* LangParser::ExpressionListContext::COMMA(size_t i) {
  return getToken(LangParser::COMMA, i);
}


size_t LangParser::ExpressionListContext::getRuleIndex() const {
  return LangParser::RuleExpressionList;
}


std::any LangParser::ExpressionListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitExpressionList(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ExpressionListContext* LangParser::expressionList() {
  ExpressionListContext *_localctx = _tracker.createInstance<ExpressionListContext>(_ctx, getState());
  enterRule(_localctx, 42, LangParser::RuleExpressionList);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(395);
    expression();
    setState(400);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::COMMA) {
      setState(396);
      match(LangParser::COMMA);
      setState(397);
      expression();
      setState(402);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LogicalOrExpContext ------------------------------------------------------------------

LangParser::LogicalOrExpContext::LogicalOrExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::LogicalOrExpContext::getRuleIndex() const {
  return LangParser::RuleLogicalOrExp;
}

void LangParser::LogicalOrExpContext::copyFrom(LogicalOrExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- LogicalOrExpressionContext ------------------------------------------------------------------

std::vector<LangParser::LogicalAndExpContext *> LangParser::LogicalOrExpressionContext::logicalAndExp() {
  return getRuleContexts<LangParser::LogicalAndExpContext>();
}

LangParser::LogicalAndExpContext* LangParser::LogicalOrExpressionContext::logicalAndExp(size_t i) {
  return getRuleContext<LangParser::LogicalAndExpContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::LogicalOrExpressionContext::OR() {
  return getTokens(LangParser::OR);
}

tree::TerminalNode* LangParser::LogicalOrExpressionContext::OR(size_t i) {
  return getToken(LangParser::OR, i);
}

LangParser::LogicalOrExpressionContext::LogicalOrExpressionContext(LogicalOrExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::LogicalOrExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitLogicalOrExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::LogicalOrExpContext* LangParser::logicalOrExp() {
  LogicalOrExpContext *_localctx = _tracker.createInstance<LogicalOrExpContext>(_ctx, getState());
  enterRule(_localctx, 44, LangParser::RuleLogicalOrExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::LogicalOrExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(403);
    logicalAndExp();
    setState(408);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::OR) {
      setState(404);
      match(LangParser::OR);
      setState(405);
      logicalAndExp();
      setState(410);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LogicalAndExpContext ------------------------------------------------------------------

LangParser::LogicalAndExpContext::LogicalAndExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::LogicalAndExpContext::getRuleIndex() const {
  return LangParser::RuleLogicalAndExp;
}

void LangParser::LogicalAndExpContext::copyFrom(LogicalAndExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- LogicalAndExpressionContext ------------------------------------------------------------------

std::vector<LangParser::BitwiseOrExpContext *> LangParser::LogicalAndExpressionContext::bitwiseOrExp() {
  return getRuleContexts<LangParser::BitwiseOrExpContext>();
}

LangParser::BitwiseOrExpContext* LangParser::LogicalAndExpressionContext::bitwiseOrExp(size_t i) {
  return getRuleContext<LangParser::BitwiseOrExpContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::LogicalAndExpressionContext::AND() {
  return getTokens(LangParser::AND);
}

tree::TerminalNode* LangParser::LogicalAndExpressionContext::AND(size_t i) {
  return getToken(LangParser::AND, i);
}

LangParser::LogicalAndExpressionContext::LogicalAndExpressionContext(LogicalAndExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::LogicalAndExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitLogicalAndExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::LogicalAndExpContext* LangParser::logicalAndExp() {
  LogicalAndExpContext *_localctx = _tracker.createInstance<LogicalAndExpContext>(_ctx, getState());
  enterRule(_localctx, 46, LangParser::RuleLogicalAndExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::LogicalAndExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(411);
    bitwiseOrExp();
    setState(416);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::AND) {
      setState(412);
      match(LangParser::AND);
      setState(413);
      bitwiseOrExp();
      setState(418);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BitwiseOrExpContext ------------------------------------------------------------------

LangParser::BitwiseOrExpContext::BitwiseOrExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::BitwiseOrExpContext::getRuleIndex() const {
  return LangParser::RuleBitwiseOrExp;
}

void LangParser::BitwiseOrExpContext::copyFrom(BitwiseOrExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- BitwiseOrExpressionContext ------------------------------------------------------------------

std::vector<LangParser::BitwiseXorExpContext *> LangParser::BitwiseOrExpressionContext::bitwiseXorExp() {
  return getRuleContexts<LangParser::BitwiseXorExpContext>();
}

LangParser::BitwiseXorExpContext* LangParser::BitwiseOrExpressionContext::bitwiseXorExp(size_t i) {
  return getRuleContext<LangParser::BitwiseXorExpContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::BitwiseOrExpressionContext::BIT_OR() {
  return getTokens(LangParser::BIT_OR);
}

tree::TerminalNode* LangParser::BitwiseOrExpressionContext::BIT_OR(size_t i) {
  return getToken(LangParser::BIT_OR, i);
}

LangParser::BitwiseOrExpressionContext::BitwiseOrExpressionContext(BitwiseOrExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::BitwiseOrExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitBitwiseOrExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::BitwiseOrExpContext* LangParser::bitwiseOrExp() {
  BitwiseOrExpContext *_localctx = _tracker.createInstance<BitwiseOrExpContext>(_ctx, getState());
  enterRule(_localctx, 48, LangParser::RuleBitwiseOrExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::BitwiseOrExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(419);
    bitwiseXorExp();
    setState(424);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::BIT_OR) {
      setState(420);
      match(LangParser::BIT_OR);
      setState(421);
      bitwiseXorExp();
      setState(426);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BitwiseXorExpContext ------------------------------------------------------------------

LangParser::BitwiseXorExpContext::BitwiseXorExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::BitwiseXorExpContext::getRuleIndex() const {
  return LangParser::RuleBitwiseXorExp;
}

void LangParser::BitwiseXorExpContext::copyFrom(BitwiseXorExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- BitwiseXorExpressionContext ------------------------------------------------------------------

std::vector<LangParser::BitwiseAndExpContext *> LangParser::BitwiseXorExpressionContext::bitwiseAndExp() {
  return getRuleContexts<LangParser::BitwiseAndExpContext>();
}

LangParser::BitwiseAndExpContext* LangParser::BitwiseXorExpressionContext::bitwiseAndExp(size_t i) {
  return getRuleContext<LangParser::BitwiseAndExpContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::BitwiseXorExpressionContext::BIT_XOR() {
  return getTokens(LangParser::BIT_XOR);
}

tree::TerminalNode* LangParser::BitwiseXorExpressionContext::BIT_XOR(size_t i) {
  return getToken(LangParser::BIT_XOR, i);
}

LangParser::BitwiseXorExpressionContext::BitwiseXorExpressionContext(BitwiseXorExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::BitwiseXorExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitBitwiseXorExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::BitwiseXorExpContext* LangParser::bitwiseXorExp() {
  BitwiseXorExpContext *_localctx = _tracker.createInstance<BitwiseXorExpContext>(_ctx, getState());
  enterRule(_localctx, 50, LangParser::RuleBitwiseXorExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::BitwiseXorExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(427);
    bitwiseAndExp();
    setState(432);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::BIT_XOR) {
      setState(428);
      match(LangParser::BIT_XOR);
      setState(429);
      bitwiseAndExp();
      setState(434);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BitwiseAndExpContext ------------------------------------------------------------------

LangParser::BitwiseAndExpContext::BitwiseAndExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::BitwiseAndExpContext::getRuleIndex() const {
  return LangParser::RuleBitwiseAndExp;
}

void LangParser::BitwiseAndExpContext::copyFrom(BitwiseAndExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- BitwiseAndExpressionContext ------------------------------------------------------------------

std::vector<LangParser::EqualityExpContext *> LangParser::BitwiseAndExpressionContext::equalityExp() {
  return getRuleContexts<LangParser::EqualityExpContext>();
}

LangParser::EqualityExpContext* LangParser::BitwiseAndExpressionContext::equalityExp(size_t i) {
  return getRuleContext<LangParser::EqualityExpContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::BitwiseAndExpressionContext::BIT_AND() {
  return getTokens(LangParser::BIT_AND);
}

tree::TerminalNode* LangParser::BitwiseAndExpressionContext::BIT_AND(size_t i) {
  return getToken(LangParser::BIT_AND, i);
}

LangParser::BitwiseAndExpressionContext::BitwiseAndExpressionContext(BitwiseAndExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::BitwiseAndExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitBitwiseAndExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::BitwiseAndExpContext* LangParser::bitwiseAndExp() {
  BitwiseAndExpContext *_localctx = _tracker.createInstance<BitwiseAndExpContext>(_ctx, getState());
  enterRule(_localctx, 52, LangParser::RuleBitwiseAndExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::BitwiseAndExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(435);
    equalityExp();
    setState(440);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::BIT_AND) {
      setState(436);
      match(LangParser::BIT_AND);
      setState(437);
      equalityExp();
      setState(442);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EqualityExpContext ------------------------------------------------------------------

LangParser::EqualityExpContext::EqualityExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::EqualityExpContext::getRuleIndex() const {
  return LangParser::RuleEqualityExp;
}

void LangParser::EqualityExpContext::copyFrom(EqualityExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- EqualityExpressionContext ------------------------------------------------------------------

std::vector<LangParser::ComparisonExpContext *> LangParser::EqualityExpressionContext::comparisonExp() {
  return getRuleContexts<LangParser::ComparisonExpContext>();
}

LangParser::ComparisonExpContext* LangParser::EqualityExpressionContext::comparisonExp(size_t i) {
  return getRuleContext<LangParser::ComparisonExpContext>(i);
}

std::vector<LangParser::EqualityExpOpContext *> LangParser::EqualityExpressionContext::equalityExpOp() {
  return getRuleContexts<LangParser::EqualityExpOpContext>();
}

LangParser::EqualityExpOpContext* LangParser::EqualityExpressionContext::equalityExpOp(size_t i) {
  return getRuleContext<LangParser::EqualityExpOpContext>(i);
}

LangParser::EqualityExpressionContext::EqualityExpressionContext(EqualityExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::EqualityExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitEqualityExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::EqualityExpContext* LangParser::equalityExp() {
  EqualityExpContext *_localctx = _tracker.createInstance<EqualityExpContext>(_ctx, getState());
  enterRule(_localctx, 54, LangParser::RuleEqualityExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::EqualityExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(443);
    comparisonExp();
    setState(449);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::EQ

    || _la == LangParser::NEQ) {
      setState(444);
      equalityExpOp();
      setState(445);
      comparisonExp();
      setState(451);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EqualityExpOpContext ------------------------------------------------------------------

LangParser::EqualityExpOpContext::EqualityExpOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::EqualityExpOpContext::EQ() {
  return getToken(LangParser::EQ, 0);
}

tree::TerminalNode* LangParser::EqualityExpOpContext::NEQ() {
  return getToken(LangParser::NEQ, 0);
}


size_t LangParser::EqualityExpOpContext::getRuleIndex() const {
  return LangParser::RuleEqualityExpOp;
}


std::any LangParser::EqualityExpOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitEqualityExpOp(this);
  else
    return visitor->visitChildren(this);
}

LangParser::EqualityExpOpContext* LangParser::equalityExpOp() {
  EqualityExpOpContext *_localctx = _tracker.createInstance<EqualityExpOpContext>(_ctx, getState());
  enterRule(_localctx, 56, LangParser::RuleEqualityExpOp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(452);
    _la = _input->LA(1);
    if (!(_la == LangParser::EQ

    || _la == LangParser::NEQ)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ComparisonExpContext ------------------------------------------------------------------

LangParser::ComparisonExpContext::ComparisonExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::ComparisonExpContext::getRuleIndex() const {
  return LangParser::RuleComparisonExp;
}

void LangParser::ComparisonExpContext::copyFrom(ComparisonExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ComparisonExpressionContext ------------------------------------------------------------------

std::vector<LangParser::ShiftExpContext *> LangParser::ComparisonExpressionContext::shiftExp() {
  return getRuleContexts<LangParser::ShiftExpContext>();
}

LangParser::ShiftExpContext* LangParser::ComparisonExpressionContext::shiftExp(size_t i) {
  return getRuleContext<LangParser::ShiftExpContext>(i);
}

std::vector<LangParser::ComparisonExpOpContext *> LangParser::ComparisonExpressionContext::comparisonExpOp() {
  return getRuleContexts<LangParser::ComparisonExpOpContext>();
}

LangParser::ComparisonExpOpContext* LangParser::ComparisonExpressionContext::comparisonExpOp(size_t i) {
  return getRuleContext<LangParser::ComparisonExpOpContext>(i);
}

LangParser::ComparisonExpressionContext::ComparisonExpressionContext(ComparisonExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::ComparisonExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitComparisonExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::ComparisonExpContext* LangParser::comparisonExp() {
  ComparisonExpContext *_localctx = _tracker.createInstance<ComparisonExpContext>(_ctx, getState());
  enterRule(_localctx, 58, LangParser::RuleComparisonExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::ComparisonExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(454);
    shiftExp();
    setState(460);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8444249301319680) != 0)) {
      setState(455);
      comparisonExpOp();
      setState(456);
      shiftExp();
      setState(462);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ComparisonExpOpContext ------------------------------------------------------------------

LangParser::ComparisonExpOpContext::ComparisonExpOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::ComparisonExpOpContext::LT() {
  return getToken(LangParser::LT, 0);
}

tree::TerminalNode* LangParser::ComparisonExpOpContext::GT() {
  return getToken(LangParser::GT, 0);
}

tree::TerminalNode* LangParser::ComparisonExpOpContext::LTE() {
  return getToken(LangParser::LTE, 0);
}

tree::TerminalNode* LangParser::ComparisonExpOpContext::GTE() {
  return getToken(LangParser::GTE, 0);
}


size_t LangParser::ComparisonExpOpContext::getRuleIndex() const {
  return LangParser::RuleComparisonExpOp;
}


std::any LangParser::ComparisonExpOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitComparisonExpOp(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ComparisonExpOpContext* LangParser::comparisonExpOp() {
  ComparisonExpOpContext *_localctx = _tracker.createInstance<ComparisonExpOpContext>(_ctx, getState());
  enterRule(_localctx, 60, LangParser::RuleComparisonExpOp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(463);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8444249301319680) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ShiftExpContext ------------------------------------------------------------------

LangParser::ShiftExpContext::ShiftExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::ShiftExpContext::getRuleIndex() const {
  return LangParser::RuleShiftExp;
}

void LangParser::ShiftExpContext::copyFrom(ShiftExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ShiftExpressionContext ------------------------------------------------------------------

std::vector<LangParser::ConcatExpContext *> LangParser::ShiftExpressionContext::concatExp() {
  return getRuleContexts<LangParser::ConcatExpContext>();
}

LangParser::ConcatExpContext* LangParser::ShiftExpressionContext::concatExp(size_t i) {
  return getRuleContext<LangParser::ConcatExpContext>(i);
}

std::vector<LangParser::ShiftExpOpContext *> LangParser::ShiftExpressionContext::shiftExpOp() {
  return getRuleContexts<LangParser::ShiftExpOpContext>();
}

LangParser::ShiftExpOpContext* LangParser::ShiftExpressionContext::shiftExpOp(size_t i) {
  return getRuleContext<LangParser::ShiftExpOpContext>(i);
}

LangParser::ShiftExpressionContext::ShiftExpressionContext(ShiftExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::ShiftExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitShiftExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::ShiftExpContext* LangParser::shiftExp() {
  ShiftExpContext *_localctx = _tracker.createInstance<ShiftExpContext>(_ctx, getState());
  enterRule(_localctx, 62, LangParser::RuleShiftExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    _localctx = _tracker.createInstance<LangParser::ShiftExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(465);
    concatExp();
    setState(471);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 51, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(466);
        shiftExpOp();
        setState(467);
        concatExp(); 
      }
      setState(473);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 51, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ShiftExpOpContext ------------------------------------------------------------------

LangParser::ShiftExpOpContext::ShiftExpOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::ShiftExpOpContext::LSHIFT() {
  return getToken(LangParser::LSHIFT, 0);
}

std::vector<tree::TerminalNode *> LangParser::ShiftExpOpContext::GT() {
  return getTokens(LangParser::GT);
}

tree::TerminalNode* LangParser::ShiftExpOpContext::GT(size_t i) {
  return getToken(LangParser::GT, i);
}


size_t LangParser::ShiftExpOpContext::getRuleIndex() const {
  return LangParser::RuleShiftExpOp;
}


std::any LangParser::ShiftExpOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitShiftExpOp(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ShiftExpOpContext* LangParser::shiftExpOp() {
  ShiftExpOpContext *_localctx = _tracker.createInstance<ShiftExpOpContext>(_ctx, getState());
  enterRule(_localctx, 64, LangParser::RuleShiftExpOp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(477);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::LSHIFT: {
        setState(474);
        match(LangParser::LSHIFT);
        break;
      }

      case LangParser::GT: {
        setState(475);
        match(LangParser::GT);
        setState(476);
        match(LangParser::GT);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConcatExpContext ------------------------------------------------------------------

LangParser::ConcatExpContext::ConcatExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::ConcatExpContext::getRuleIndex() const {
  return LangParser::RuleConcatExp;
}

void LangParser::ConcatExpContext::copyFrom(ConcatExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ConcatExpressionContext ------------------------------------------------------------------

std::vector<LangParser::AddSubExpContext *> LangParser::ConcatExpressionContext::addSubExp() {
  return getRuleContexts<LangParser::AddSubExpContext>();
}

LangParser::AddSubExpContext* LangParser::ConcatExpressionContext::addSubExp(size_t i) {
  return getRuleContext<LangParser::AddSubExpContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::ConcatExpressionContext::CONCAT() {
  return getTokens(LangParser::CONCAT);
}

tree::TerminalNode* LangParser::ConcatExpressionContext::CONCAT(size_t i) {
  return getToken(LangParser::CONCAT, i);
}

LangParser::ConcatExpressionContext::ConcatExpressionContext(ConcatExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::ConcatExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitConcatExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::ConcatExpContext* LangParser::concatExp() {
  ConcatExpContext *_localctx = _tracker.createInstance<ConcatExpContext>(_ctx, getState());
  enterRule(_localctx, 66, LangParser::RuleConcatExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::ConcatExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(479);
    addSubExp();
    setState(484);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::CONCAT) {
      setState(480);
      match(LangParser::CONCAT);
      setState(481);
      addSubExp();
      setState(486);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AddSubExpContext ------------------------------------------------------------------

LangParser::AddSubExpContext::AddSubExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::AddSubExpContext::getRuleIndex() const {
  return LangParser::RuleAddSubExp;
}

void LangParser::AddSubExpContext::copyFrom(AddSubExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- AddSubExpressionContext ------------------------------------------------------------------

std::vector<LangParser::MulDivModExpContext *> LangParser::AddSubExpressionContext::mulDivModExp() {
  return getRuleContexts<LangParser::MulDivModExpContext>();
}

LangParser::MulDivModExpContext* LangParser::AddSubExpressionContext::mulDivModExp(size_t i) {
  return getRuleContext<LangParser::MulDivModExpContext>(i);
}

std::vector<LangParser::AddSubExpOpContext *> LangParser::AddSubExpressionContext::addSubExpOp() {
  return getRuleContexts<LangParser::AddSubExpOpContext>();
}

LangParser::AddSubExpOpContext* LangParser::AddSubExpressionContext::addSubExpOp(size_t i) {
  return getRuleContext<LangParser::AddSubExpOpContext>(i);
}

LangParser::AddSubExpressionContext::AddSubExpressionContext(AddSubExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::AddSubExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitAddSubExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::AddSubExpContext* LangParser::addSubExp() {
  AddSubExpContext *_localctx = _tracker.createInstance<AddSubExpContext>(_ctx, getState());
  enterRule(_localctx, 68, LangParser::RuleAddSubExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::AddSubExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(487);
    mulDivModExp();
    setState(493);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::ADD

    || _la == LangParser::SUB) {
      setState(488);
      addSubExpOp();
      setState(489);
      mulDivModExp();
      setState(495);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AddSubExpOpContext ------------------------------------------------------------------

LangParser::AddSubExpOpContext::AddSubExpOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::AddSubExpOpContext::ADD() {
  return getToken(LangParser::ADD, 0);
}

tree::TerminalNode* LangParser::AddSubExpOpContext::SUB() {
  return getToken(LangParser::SUB, 0);
}


size_t LangParser::AddSubExpOpContext::getRuleIndex() const {
  return LangParser::RuleAddSubExpOp;
}


std::any LangParser::AddSubExpOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitAddSubExpOp(this);
  else
    return visitor->visitChildren(this);
}

LangParser::AddSubExpOpContext* LangParser::addSubExpOp() {
  AddSubExpOpContext *_localctx = _tracker.createInstance<AddSubExpOpContext>(_ctx, getState());
  enterRule(_localctx, 70, LangParser::RuleAddSubExpOp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(496);
    _la = _input->LA(1);
    if (!(_la == LangParser::ADD

    || _la == LangParser::SUB)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MulDivModExpContext ------------------------------------------------------------------

LangParser::MulDivModExpContext::MulDivModExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::MulDivModExpContext::getRuleIndex() const {
  return LangParser::RuleMulDivModExp;
}

void LangParser::MulDivModExpContext::copyFrom(MulDivModExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- MulDivModExpressionContext ------------------------------------------------------------------

std::vector<LangParser::UnaryExpContext *> LangParser::MulDivModExpressionContext::unaryExp() {
  return getRuleContexts<LangParser::UnaryExpContext>();
}

LangParser::UnaryExpContext* LangParser::MulDivModExpressionContext::unaryExp(size_t i) {
  return getRuleContext<LangParser::UnaryExpContext>(i);
}

std::vector<LangParser::MulDivModExpOpContext *> LangParser::MulDivModExpressionContext::mulDivModExpOp() {
  return getRuleContexts<LangParser::MulDivModExpOpContext>();
}

LangParser::MulDivModExpOpContext* LangParser::MulDivModExpressionContext::mulDivModExpOp(size_t i) {
  return getRuleContext<LangParser::MulDivModExpOpContext>(i);
}

LangParser::MulDivModExpressionContext::MulDivModExpressionContext(MulDivModExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::MulDivModExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMulDivModExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::MulDivModExpContext* LangParser::mulDivModExp() {
  MulDivModExpContext *_localctx = _tracker.createInstance<MulDivModExpContext>(_ctx, getState());
  enterRule(_localctx, 72, LangParser::RuleMulDivModExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::MulDivModExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(498);
    unaryExp();
    setState(504);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 962072674304) != 0)) {
      setState(499);
      mulDivModExpOp();
      setState(500);
      unaryExp();
      setState(506);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MulDivModExpOpContext ------------------------------------------------------------------

LangParser::MulDivModExpOpContext::MulDivModExpOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::MulDivModExpOpContext::MUL() {
  return getToken(LangParser::MUL, 0);
}

tree::TerminalNode* LangParser::MulDivModExpOpContext::DIV() {
  return getToken(LangParser::DIV, 0);
}

tree::TerminalNode* LangParser::MulDivModExpOpContext::MOD() {
  return getToken(LangParser::MOD, 0);
}


size_t LangParser::MulDivModExpOpContext::getRuleIndex() const {
  return LangParser::RuleMulDivModExpOp;
}


std::any LangParser::MulDivModExpOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMulDivModExpOp(this);
  else
    return visitor->visitChildren(this);
}

LangParser::MulDivModExpOpContext* LangParser::mulDivModExpOp() {
  MulDivModExpOpContext *_localctx = _tracker.createInstance<MulDivModExpOpContext>(_ctx, getState());
  enterRule(_localctx, 74, LangParser::RuleMulDivModExpOp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(507);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 962072674304) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnaryExpContext ------------------------------------------------------------------

LangParser::UnaryExpContext::UnaryExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::UnaryExpContext::getRuleIndex() const {
  return LangParser::RuleUnaryExp;
}

void LangParser::UnaryExpContext::copyFrom(UnaryExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- UnaryPrefixContext ------------------------------------------------------------------

LangParser::UnaryExpContext* LangParser::UnaryPrefixContext::unaryExp() {
  return getRuleContext<LangParser::UnaryExpContext>(0);
}

tree::TerminalNode* LangParser::UnaryPrefixContext::NOT() {
  return getToken(LangParser::NOT, 0);
}

tree::TerminalNode* LangParser::UnaryPrefixContext::SUB() {
  return getToken(LangParser::SUB, 0);
}

tree::TerminalNode* LangParser::UnaryPrefixContext::LEN() {
  return getToken(LangParser::LEN, 0);
}

tree::TerminalNode* LangParser::UnaryPrefixContext::BIT_NOT() {
  return getToken(LangParser::BIT_NOT, 0);
}

LangParser::UnaryPrefixContext::UnaryPrefixContext(UnaryExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::UnaryPrefixContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitUnaryPrefix(this);
  else
    return visitor->visitChildren(this);
}
//----------------- UnaryToPostfixContext ------------------------------------------------------------------

LangParser::PostfixExpContext* LangParser::UnaryToPostfixContext::postfixExp() {
  return getRuleContext<LangParser::PostfixExpContext>(0);
}

LangParser::UnaryToPostfixContext::UnaryToPostfixContext(UnaryExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::UnaryToPostfixContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitUnaryToPostfix(this);
  else
    return visitor->visitChildren(this);
}
LangParser::UnaryExpContext* LangParser::unaryExp() {
  UnaryExpContext *_localctx = _tracker.createInstance<UnaryExpContext>(_ctx, getState());
  enterRule(_localctx, 76, LangParser::RuleUnaryExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(512);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::SUB:
      case LangParser::NOT:
      case LangParser::LEN:
      case LangParser::BIT_NOT: {
        _localctx = _tracker.createInstance<LangParser::UnaryPrefixContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(509);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 2485987063027990528) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(510);
        unaryExp();
        break;
      }

      case LangParser::NULL_:
      case LangParser::FUNCTION:
      case LangParser::TRUE:
      case LangParser::FALSE:
      case LangParser::NEW:
      case LangParser::OP:
      case LangParser::OSB:
      case LangParser::OCB:
      case LangParser::DDD:
      case LangParser::INTEGER:
      case LangParser::FLOAT_LITERAL:
      case LangParser::STRING_LITERAL:
      case LangParser::IDENTIFIER: {
        _localctx = _tracker.createInstance<LangParser::UnaryToPostfixContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(511);
        postfixExp();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PostfixExpContext ------------------------------------------------------------------

LangParser::PostfixExpContext::PostfixExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::PostfixExpContext::getRuleIndex() const {
  return LangParser::RulePostfixExp;
}

void LangParser::PostfixExpContext::copyFrom(PostfixExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- PostfixExpressionContext ------------------------------------------------------------------

LangParser::PrimaryExpContext* LangParser::PostfixExpressionContext::primaryExp() {
  return getRuleContext<LangParser::PrimaryExpContext>(0);
}

std::vector<LangParser::PostfixSuffixContext *> LangParser::PostfixExpressionContext::postfixSuffix() {
  return getRuleContexts<LangParser::PostfixSuffixContext>();
}

LangParser::PostfixSuffixContext* LangParser::PostfixExpressionContext::postfixSuffix(size_t i) {
  return getRuleContext<LangParser::PostfixSuffixContext>(i);
}

LangParser::PostfixExpressionContext::PostfixExpressionContext(PostfixExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::PostfixExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPostfixExpression(this);
  else
    return visitor->visitChildren(this);
}
LangParser::PostfixExpContext* LangParser::postfixExp() {
  PostfixExpContext *_localctx = _tracker.createInstance<PostfixExpContext>(_ctx, getState());
  enterRule(_localctx, 78, LangParser::RulePostfixExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::PostfixExpressionContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(514);
    primaryExp();
    setState(518);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 389) != 0)) {
      setState(515);
      postfixSuffix();
      setState(520);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PostfixSuffixContext ------------------------------------------------------------------

LangParser::PostfixSuffixContext::PostfixSuffixContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::PostfixSuffixContext::getRuleIndex() const {
  return LangParser::RulePostfixSuffix;
}

void LangParser::PostfixSuffixContext::copyFrom(PostfixSuffixContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- PostfixMemberSuffixContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::PostfixMemberSuffixContext::DOT() {
  return getToken(LangParser::DOT, 0);
}

tree::TerminalNode* LangParser::PostfixMemberSuffixContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

LangParser::PostfixMemberSuffixContext::PostfixMemberSuffixContext(PostfixSuffixContext *ctx) { copyFrom(ctx); }


std::any LangParser::PostfixMemberSuffixContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPostfixMemberSuffix(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PostfixColonLookupSuffixContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::PostfixColonLookupSuffixContext::COL() {
  return getToken(LangParser::COL, 0);
}

tree::TerminalNode* LangParser::PostfixColonLookupSuffixContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

LangParser::PostfixColonLookupSuffixContext::PostfixColonLookupSuffixContext(PostfixSuffixContext *ctx) { copyFrom(ctx); }


std::any LangParser::PostfixColonLookupSuffixContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPostfixColonLookupSuffix(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PostfixIndexSuffixContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::PostfixIndexSuffixContext::OSB() {
  return getToken(LangParser::OSB, 0);
}

LangParser::ExpressionContext* LangParser::PostfixIndexSuffixContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

tree::TerminalNode* LangParser::PostfixIndexSuffixContext::CSB() {
  return getToken(LangParser::CSB, 0);
}

LangParser::PostfixIndexSuffixContext::PostfixIndexSuffixContext(PostfixSuffixContext *ctx) { copyFrom(ctx); }


std::any LangParser::PostfixIndexSuffixContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPostfixIndexSuffix(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PostfixCallSuffixContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::PostfixCallSuffixContext::OP() {
  return getToken(LangParser::OP, 0);
}

tree::TerminalNode* LangParser::PostfixCallSuffixContext::CP() {
  return getToken(LangParser::CP, 0);
}

LangParser::ArgumentsContext* LangParser::PostfixCallSuffixContext::arguments() {
  return getRuleContext<LangParser::ArgumentsContext>(0);
}

LangParser::PostfixCallSuffixContext::PostfixCallSuffixContext(PostfixSuffixContext *ctx) { copyFrom(ctx); }


std::any LangParser::PostfixCallSuffixContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPostfixCallSuffix(this);
  else
    return visitor->visitChildren(this);
}
LangParser::PostfixSuffixContext* LangParser::postfixSuffix() {
  PostfixSuffixContext *_localctx = _tracker.createInstance<PostfixSuffixContext>(_ctx, getState());
  enterRule(_localctx, 80, LangParser::RulePostfixSuffix);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(534);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::OSB: {
        _localctx = _tracker.createInstance<LangParser::PostfixIndexSuffixContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(521);
        match(LangParser::OSB);
        setState(522);
        expression();
        setState(523);
        match(LangParser::CSB);
        break;
      }

      case LangParser::DOT: {
        _localctx = _tracker.createInstance<LangParser::PostfixMemberSuffixContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(525);
        match(LangParser::DOT);
        setState(526);
        match(LangParser::IDENTIFIER);
        break;
      }

      case LangParser::COL: {
        _localctx = _tracker.createInstance<LangParser::PostfixColonLookupSuffixContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(527);
        match(LangParser::COL);
        setState(528);
        match(LangParser::IDENTIFIER);
        break;
      }

      case LangParser::OP: {
        _localctx = _tracker.createInstance<LangParser::PostfixCallSuffixContext>(_localctx);
        enterOuterAlt(_localctx, 4);
        setState(529);
        match(LangParser::OP);
        setState(531);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 58, _ctx)) {
        case 1: {
          setState(530);
          arguments();
          break;
        }

        default:
          break;
        }
        setState(533);
        match(LangParser::CP);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimaryExpContext ------------------------------------------------------------------

LangParser::PrimaryExpContext::PrimaryExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::PrimaryExpContext::getRuleIndex() const {
  return LangParser::RulePrimaryExp;
}

void LangParser::PrimaryExpContext::copyFrom(PrimaryExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- PrimaryAtomContext ------------------------------------------------------------------

LangParser::AtomexpContext* LangParser::PrimaryAtomContext::atomexp() {
  return getRuleContext<LangParser::AtomexpContext>(0);
}

LangParser::PrimaryAtomContext::PrimaryAtomContext(PrimaryExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::PrimaryAtomContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPrimaryAtom(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PrimaryParenExpContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::PrimaryParenExpContext::OP() {
  return getToken(LangParser::OP, 0);
}

LangParser::ExpressionContext* LangParser::PrimaryParenExpContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

tree::TerminalNode* LangParser::PrimaryParenExpContext::CP() {
  return getToken(LangParser::CP, 0);
}

LangParser::PrimaryParenExpContext::PrimaryParenExpContext(PrimaryExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::PrimaryParenExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPrimaryParenExp(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PrimaryLambdaContext ------------------------------------------------------------------

LangParser::LambdaExpressionContext* LangParser::PrimaryLambdaContext::lambdaExpression() {
  return getRuleContext<LangParser::LambdaExpressionContext>(0);
}

LangParser::PrimaryLambdaContext::PrimaryLambdaContext(PrimaryExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::PrimaryLambdaContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPrimaryLambda(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PrimaryListLiteralContext ------------------------------------------------------------------

LangParser::ListExpressionContext* LangParser::PrimaryListLiteralContext::listExpression() {
  return getRuleContext<LangParser::ListExpressionContext>(0);
}

LangParser::PrimaryListLiteralContext::PrimaryListLiteralContext(PrimaryExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::PrimaryListLiteralContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPrimaryListLiteral(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PrimaryMapLiteralContext ------------------------------------------------------------------

LangParser::MapExpressionContext* LangParser::PrimaryMapLiteralContext::mapExpression() {
  return getRuleContext<LangParser::MapExpressionContext>(0);
}

LangParser::PrimaryMapLiteralContext::PrimaryMapLiteralContext(PrimaryExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::PrimaryMapLiteralContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPrimaryMapLiteral(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PrimaryIdentifierContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::PrimaryIdentifierContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

LangParser::PrimaryIdentifierContext::PrimaryIdentifierContext(PrimaryExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::PrimaryIdentifierContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPrimaryIdentifier(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PrimaryNewContext ------------------------------------------------------------------

LangParser::NewExpContext* LangParser::PrimaryNewContext::newExp() {
  return getRuleContext<LangParser::NewExpContext>(0);
}

LangParser::PrimaryNewContext::PrimaryNewContext(PrimaryExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::PrimaryNewContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPrimaryNew(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PrimaryVarArgsContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::PrimaryVarArgsContext::DDD() {
  return getToken(LangParser::DDD, 0);
}

LangParser::PrimaryVarArgsContext::PrimaryVarArgsContext(PrimaryExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::PrimaryVarArgsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitPrimaryVarArgs(this);
  else
    return visitor->visitChildren(this);
}
LangParser::PrimaryExpContext* LangParser::primaryExp() {
  PrimaryExpContext *_localctx = _tracker.createInstance<PrimaryExpContext>(_ctx, getState());
  enterRule(_localctx, 82, LangParser::RulePrimaryExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(547);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::NULL_:
      case LangParser::TRUE:
      case LangParser::FALSE:
      case LangParser::INTEGER:
      case LangParser::FLOAT_LITERAL:
      case LangParser::STRING_LITERAL: {
        _localctx = _tracker.createInstance<LangParser::PrimaryAtomContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(536);
        atomexp();
        break;
      }

      case LangParser::OSB: {
        _localctx = _tracker.createInstance<LangParser::PrimaryListLiteralContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(537);
        listExpression();
        break;
      }

      case LangParser::OCB: {
        _localctx = _tracker.createInstance<LangParser::PrimaryMapLiteralContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(538);
        mapExpression();
        break;
      }

      case LangParser::IDENTIFIER: {
        _localctx = _tracker.createInstance<LangParser::PrimaryIdentifierContext>(_localctx);
        enterOuterAlt(_localctx, 4);
        setState(539);
        match(LangParser::IDENTIFIER);
        break;
      }

      case LangParser::DDD: {
        _localctx = _tracker.createInstance<LangParser::PrimaryVarArgsContext>(_localctx);
        enterOuterAlt(_localctx, 5);
        setState(540);
        match(LangParser::DDD);
        break;
      }

      case LangParser::OP: {
        _localctx = _tracker.createInstance<LangParser::PrimaryParenExpContext>(_localctx);
        enterOuterAlt(_localctx, 6);
        setState(541);
        match(LangParser::OP);
        setState(542);
        expression();
        setState(543);
        match(LangParser::CP);
        break;
      }

      case LangParser::NEW: {
        _localctx = _tracker.createInstance<LangParser::PrimaryNewContext>(_localctx);
        enterOuterAlt(_localctx, 7);
        setState(545);
        newExp();
        break;
      }

      case LangParser::FUNCTION: {
        _localctx = _tracker.createInstance<LangParser::PrimaryLambdaContext>(_localctx);
        enterOuterAlt(_localctx, 8);
        setState(546);
        lambdaExpression();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AtomexpContext ------------------------------------------------------------------

LangParser::AtomexpContext::AtomexpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::AtomexpContext::NULL_() {
  return getToken(LangParser::NULL_, 0);
}

tree::TerminalNode* LangParser::AtomexpContext::TRUE() {
  return getToken(LangParser::TRUE, 0);
}

tree::TerminalNode* LangParser::AtomexpContext::FALSE() {
  return getToken(LangParser::FALSE, 0);
}

tree::TerminalNode* LangParser::AtomexpContext::INTEGER() {
  return getToken(LangParser::INTEGER, 0);
}

tree::TerminalNode* LangParser::AtomexpContext::FLOAT_LITERAL() {
  return getToken(LangParser::FLOAT_LITERAL, 0);
}

tree::TerminalNode* LangParser::AtomexpContext::STRING_LITERAL() {
  return getToken(LangParser::STRING_LITERAL, 0);
}


size_t LangParser::AtomexpContext::getRuleIndex() const {
  return LangParser::RuleAtomexp;
}


std::any LangParser::AtomexpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitAtomexp(this);
  else
    return visitor->visitChildren(this);
}

LangParser::AtomexpContext* LangParser::atomexp() {
  AtomexpContext *_localctx = _tracker.createInstance<AtomexpContext>(_ctx, getState());
  enterRule(_localctx, 84, LangParser::RuleAtomexp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(549);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 6291712) != 0) || ((((_la - 75) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 75)) & 7) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LambdaExpressionContext ------------------------------------------------------------------

LangParser::LambdaExpressionContext::LambdaExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::LambdaExpressionContext::getRuleIndex() const {
  return LangParser::RuleLambdaExpression;
}

void LangParser::LambdaExpressionContext::copyFrom(LambdaExpressionContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- LambdaExprDefContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::LambdaExprDefContext::FUNCTION() {
  return getToken(LangParser::FUNCTION, 0);
}

tree::TerminalNode* LangParser::LambdaExprDefContext::OP() {
  return getToken(LangParser::OP, 0);
}

tree::TerminalNode* LangParser::LambdaExprDefContext::CP() {
  return getToken(LangParser::CP, 0);
}

tree::TerminalNode* LangParser::LambdaExprDefContext::ARROW() {
  return getToken(LangParser::ARROW, 0);
}

LangParser::BlockStatementContext* LangParser::LambdaExprDefContext::blockStatement() {
  return getRuleContext<LangParser::BlockStatementContext>(0);
}

LangParser::TypeContext* LangParser::LambdaExprDefContext::type() {
  return getRuleContext<LangParser::TypeContext>(0);
}

tree::TerminalNode* LangParser::LambdaExprDefContext::MUTIVAR() {
  return getToken(LangParser::MUTIVAR, 0);
}

LangParser::ParameterListContext* LangParser::LambdaExprDefContext::parameterList() {
  return getRuleContext<LangParser::ParameterListContext>(0);
}

LangParser::LambdaExprDefContext::LambdaExprDefContext(LambdaExpressionContext *ctx) { copyFrom(ctx); }


std::any LangParser::LambdaExprDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitLambdaExprDef(this);
  else
    return visitor->visitChildren(this);
}
LangParser::LambdaExpressionContext* LangParser::lambdaExpression() {
  LambdaExpressionContext *_localctx = _tracker.createInstance<LambdaExpressionContext>(_ctx, getState());
  enterRule(_localctx, 86, LangParser::RuleLambdaExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::LambdaExprDefContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(551);
    match(LangParser::FUNCTION);
    setState(552);
    match(LangParser::OP);
    setState(554);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8190) != 0) || _la == LangParser::DDD

    || _la == LangParser::IDENTIFIER) {
      setState(553);
      parameterList();
    }
    setState(556);
    match(LangParser::CP);
    setState(557);
    match(LangParser::ARROW);
    setState(560);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::INT:
      case LangParser::FLOAT:
      case LangParser::NUMBER:
      case LangParser::STRING:
      case LangParser::BOOL:
      case LangParser::ANY:
      case LangParser::VOID:
      case LangParser::NULL_:
      case LangParser::LIST:
      case LangParser::MAP:
      case LangParser::FUNCTION:
      case LangParser::COROUTINE:
      case LangParser::IDENTIFIER: {
        setState(558);
        type();
        break;
      }

      case LangParser::MUTIVAR: {
        setState(559);
        match(LangParser::MUTIVAR);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(562);
    blockStatement();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ListExpressionContext ------------------------------------------------------------------

LangParser::ListExpressionContext::ListExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::ListExpressionContext::getRuleIndex() const {
  return LangParser::RuleListExpression;
}

void LangParser::ListExpressionContext::copyFrom(ListExpressionContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ListLiteralDefContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::ListLiteralDefContext::OSB() {
  return getToken(LangParser::OSB, 0);
}

tree::TerminalNode* LangParser::ListLiteralDefContext::CSB() {
  return getToken(LangParser::CSB, 0);
}

LangParser::ExpressionListContext* LangParser::ListLiteralDefContext::expressionList() {
  return getRuleContext<LangParser::ExpressionListContext>(0);
}

LangParser::ListLiteralDefContext::ListLiteralDefContext(ListExpressionContext *ctx) { copyFrom(ctx); }


std::any LangParser::ListLiteralDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitListLiteralDef(this);
  else
    return visitor->visitChildren(this);
}
LangParser::ListExpressionContext* LangParser::listExpression() {
  ListExpressionContext *_localctx = _tracker.createInstance<ListExpressionContext>(_ctx, getState());
  enterRule(_localctx, 88, LangParser::RuleListExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::ListLiteralDefContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(564);
    match(LangParser::OSB);
    setState(566);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2485987080214153472) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 31765) != 0)) {
      setState(565);
      expressionList();
    }
    setState(568);
    match(LangParser::CSB);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MapExpressionContext ------------------------------------------------------------------

LangParser::MapExpressionContext::MapExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::MapExpressionContext::getRuleIndex() const {
  return LangParser::RuleMapExpression;
}

void LangParser::MapExpressionContext::copyFrom(MapExpressionContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- MapLiteralDefContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::MapLiteralDefContext::OCB() {
  return getToken(LangParser::OCB, 0);
}

tree::TerminalNode* LangParser::MapLiteralDefContext::CCB() {
  return getToken(LangParser::CCB, 0);
}

LangParser::MapEntryListContext* LangParser::MapLiteralDefContext::mapEntryList() {
  return getRuleContext<LangParser::MapEntryListContext>(0);
}

LangParser::MapLiteralDefContext::MapLiteralDefContext(MapExpressionContext *ctx) { copyFrom(ctx); }


std::any LangParser::MapLiteralDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMapLiteralDef(this);
  else
    return visitor->visitChildren(this);
}
LangParser::MapExpressionContext* LangParser::mapExpression() {
  MapExpressionContext *_localctx = _tracker.createInstance<MapExpressionContext>(_ctx, getState());
  enterRule(_localctx, 90, LangParser::RuleMapExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::MapLiteralDefContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(570);
    match(LangParser::OCB);
    setState(572);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 66) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 66)) & 6145) != 0)) {
      setState(571);
      mapEntryList();
    }
    setState(574);
    match(LangParser::CCB);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MapEntryListContext ------------------------------------------------------------------

LangParser::MapEntryListContext::MapEntryListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<LangParser::MapEntryContext *> LangParser::MapEntryListContext::mapEntry() {
  return getRuleContexts<LangParser::MapEntryContext>();
}

LangParser::MapEntryContext* LangParser::MapEntryListContext::mapEntry(size_t i) {
  return getRuleContext<LangParser::MapEntryContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::MapEntryListContext::COMMA() {
  return getTokens(LangParser::COMMA);
}

tree::TerminalNode* LangParser::MapEntryListContext::COMMA(size_t i) {
  return getToken(LangParser::COMMA, i);
}


size_t LangParser::MapEntryListContext::getRuleIndex() const {
  return LangParser::RuleMapEntryList;
}


std::any LangParser::MapEntryListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMapEntryList(this);
  else
    return visitor->visitChildren(this);
}

LangParser::MapEntryListContext* LangParser::mapEntryList() {
  MapEntryListContext *_localctx = _tracker.createInstance<MapEntryListContext>(_ctx, getState());
  enterRule(_localctx, 92, LangParser::RuleMapEntryList);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(576);
    mapEntry();
    setState(581);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::COMMA) {
      setState(577);
      match(LangParser::COMMA);
      setState(578);
      mapEntry();
      setState(583);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MapEntryContext ------------------------------------------------------------------

LangParser::MapEntryContext::MapEntryContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::MapEntryContext::getRuleIndex() const {
  return LangParser::RuleMapEntry;
}

void LangParser::MapEntryContext::copyFrom(MapEntryContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- MapEntryExprKeyContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::MapEntryExprKeyContext::OSB() {
  return getToken(LangParser::OSB, 0);
}

std::vector<LangParser::ExpressionContext *> LangParser::MapEntryExprKeyContext::expression() {
  return getRuleContexts<LangParser::ExpressionContext>();
}

LangParser::ExpressionContext* LangParser::MapEntryExprKeyContext::expression(size_t i) {
  return getRuleContext<LangParser::ExpressionContext>(i);
}

tree::TerminalNode* LangParser::MapEntryExprKeyContext::CSB() {
  return getToken(LangParser::CSB, 0);
}

tree::TerminalNode* LangParser::MapEntryExprKeyContext::COL() {
  return getToken(LangParser::COL, 0);
}

LangParser::MapEntryExprKeyContext::MapEntryExprKeyContext(MapEntryContext *ctx) { copyFrom(ctx); }


std::any LangParser::MapEntryExprKeyContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMapEntryExprKey(this);
  else
    return visitor->visitChildren(this);
}
//----------------- MapEntryIdentKeyContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::MapEntryIdentKeyContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

tree::TerminalNode* LangParser::MapEntryIdentKeyContext::COL() {
  return getToken(LangParser::COL, 0);
}

LangParser::ExpressionContext* LangParser::MapEntryIdentKeyContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

LangParser::MapEntryIdentKeyContext::MapEntryIdentKeyContext(MapEntryContext *ctx) { copyFrom(ctx); }


std::any LangParser::MapEntryIdentKeyContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMapEntryIdentKey(this);
  else
    return visitor->visitChildren(this);
}
//----------------- MapEntryStringKeyContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::MapEntryStringKeyContext::STRING_LITERAL() {
  return getToken(LangParser::STRING_LITERAL, 0);
}

tree::TerminalNode* LangParser::MapEntryStringKeyContext::COL() {
  return getToken(LangParser::COL, 0);
}

LangParser::ExpressionContext* LangParser::MapEntryStringKeyContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

LangParser::MapEntryStringKeyContext::MapEntryStringKeyContext(MapEntryContext *ctx) { copyFrom(ctx); }


std::any LangParser::MapEntryStringKeyContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMapEntryStringKey(this);
  else
    return visitor->visitChildren(this);
}
LangParser::MapEntryContext* LangParser::mapEntry() {
  MapEntryContext *_localctx = _tracker.createInstance<MapEntryContext>(_ctx, getState());
  enterRule(_localctx, 94, LangParser::RuleMapEntry);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(596);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::IDENTIFIER: {
        _localctx = _tracker.createInstance<LangParser::MapEntryIdentKeyContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(584);
        match(LangParser::IDENTIFIER);
        setState(585);
        match(LangParser::COL);
        setState(586);
        expression();
        break;
      }

      case LangParser::OSB: {
        _localctx = _tracker.createInstance<LangParser::MapEntryExprKeyContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(587);
        match(LangParser::OSB);
        setState(588);
        expression();
        setState(589);
        match(LangParser::CSB);
        setState(590);
        match(LangParser::COL);
        setState(591);
        expression();
        break;
      }

      case LangParser::STRING_LITERAL: {
        _localctx = _tracker.createInstance<LangParser::MapEntryStringKeyContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(593);
        match(LangParser::STRING_LITERAL);
        setState(594);
        match(LangParser::COL);
        setState(595);
        expression();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- NewExpContext ------------------------------------------------------------------

LangParser::NewExpContext::NewExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::NewExpContext::getRuleIndex() const {
  return LangParser::RuleNewExp;
}

void LangParser::NewExpContext::copyFrom(NewExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- NewExpressionDefContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::NewExpressionDefContext::NEW() {
  return getToken(LangParser::NEW, 0);
}

LangParser::QualifiedIdentifierContext* LangParser::NewExpressionDefContext::qualifiedIdentifier() {
  return getRuleContext<LangParser::QualifiedIdentifierContext>(0);
}

tree::TerminalNode* LangParser::NewExpressionDefContext::OP() {
  return getToken(LangParser::OP, 0);
}

tree::TerminalNode* LangParser::NewExpressionDefContext::CP() {
  return getToken(LangParser::CP, 0);
}

LangParser::ArgumentsContext* LangParser::NewExpressionDefContext::arguments() {
  return getRuleContext<LangParser::ArgumentsContext>(0);
}

LangParser::NewExpressionDefContext::NewExpressionDefContext(NewExpContext *ctx) { copyFrom(ctx); }


std::any LangParser::NewExpressionDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitNewExpressionDef(this);
  else
    return visitor->visitChildren(this);
}
LangParser::NewExpContext* LangParser::newExp() {
  NewExpContext *_localctx = _tracker.createInstance<NewExpContext>(_ctx, getState());
  enterRule(_localctx, 96, LangParser::RuleNewExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::NewExpressionDefContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(598);
    match(LangParser::NEW);
    setState(599);
    qualifiedIdentifier();

    setState(600);
    match(LangParser::OP);
    setState(602);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 67, _ctx)) {
    case 1: {
      setState(601);
      arguments();
      break;
    }

    default:
      break;
    }
    setState(604);
    match(LangParser::CP);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- IfStatementContext ------------------------------------------------------------------

LangParser::IfStatementContext::IfStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> LangParser::IfStatementContext::IF() {
  return getTokens(LangParser::IF);
}

tree::TerminalNode* LangParser::IfStatementContext::IF(size_t i) {
  return getToken(LangParser::IF, i);
}

std::vector<tree::TerminalNode *> LangParser::IfStatementContext::OP() {
  return getTokens(LangParser::OP);
}

tree::TerminalNode* LangParser::IfStatementContext::OP(size_t i) {
  return getToken(LangParser::OP, i);
}

std::vector<LangParser::ExpressionContext *> LangParser::IfStatementContext::expression() {
  return getRuleContexts<LangParser::ExpressionContext>();
}

LangParser::ExpressionContext* LangParser::IfStatementContext::expression(size_t i) {
  return getRuleContext<LangParser::ExpressionContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::IfStatementContext::CP() {
  return getTokens(LangParser::CP);
}

tree::TerminalNode* LangParser::IfStatementContext::CP(size_t i) {
  return getToken(LangParser::CP, i);
}

std::vector<LangParser::BlockStatementContext *> LangParser::IfStatementContext::blockStatement() {
  return getRuleContexts<LangParser::BlockStatementContext>();
}

LangParser::BlockStatementContext* LangParser::IfStatementContext::blockStatement(size_t i) {
  return getRuleContext<LangParser::BlockStatementContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::IfStatementContext::ELSE() {
  return getTokens(LangParser::ELSE);
}

tree::TerminalNode* LangParser::IfStatementContext::ELSE(size_t i) {
  return getToken(LangParser::ELSE, i);
}


size_t LangParser::IfStatementContext::getRuleIndex() const {
  return LangParser::RuleIfStatement;
}


std::any LangParser::IfStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitIfStatement(this);
  else
    return visitor->visitChildren(this);
}

LangParser::IfStatementContext* LangParser::ifStatement() {
  IfStatementContext *_localctx = _tracker.createInstance<IfStatementContext>(_ctx, getState());
  enterRule(_localctx, 98, LangParser::RuleIfStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(606);
    match(LangParser::IF);
    setState(607);
    match(LangParser::OP);
    setState(608);
    expression();
    setState(609);
    match(LangParser::CP);
    setState(610);
    blockStatement();
    setState(620);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(611);
        match(LangParser::ELSE);
        setState(612);
        match(LangParser::IF);
        setState(613);
        match(LangParser::OP);
        setState(614);
        expression();
        setState(615);
        match(LangParser::CP);
        setState(616);
        blockStatement(); 
      }
      setState(622);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx);
    }
    setState(625);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::ELSE) {
      setState(623);
      match(LangParser::ELSE);
      setState(624);
      blockStatement();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- WhileStatementContext ------------------------------------------------------------------

LangParser::WhileStatementContext::WhileStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::WhileStatementContext::WHILE() {
  return getToken(LangParser::WHILE, 0);
}

tree::TerminalNode* LangParser::WhileStatementContext::OP() {
  return getToken(LangParser::OP, 0);
}

LangParser::ExpressionContext* LangParser::WhileStatementContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

tree::TerminalNode* LangParser::WhileStatementContext::CP() {
  return getToken(LangParser::CP, 0);
}

LangParser::BlockStatementContext* LangParser::WhileStatementContext::blockStatement() {
  return getRuleContext<LangParser::BlockStatementContext>(0);
}


size_t LangParser::WhileStatementContext::getRuleIndex() const {
  return LangParser::RuleWhileStatement;
}


std::any LangParser::WhileStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitWhileStatement(this);
  else
    return visitor->visitChildren(this);
}

LangParser::WhileStatementContext* LangParser::whileStatement() {
  WhileStatementContext *_localctx = _tracker.createInstance<WhileStatementContext>(_ctx, getState());
  enterRule(_localctx, 100, LangParser::RuleWhileStatement);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(627);
    match(LangParser::WHILE);
    setState(628);
    match(LangParser::OP);
    setState(629);
    expression();
    setState(630);
    match(LangParser::CP);
    setState(631);
    blockStatement();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ForStatementContext ------------------------------------------------------------------

LangParser::ForStatementContext::ForStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* LangParser::ForStatementContext::FOR() {
  return getToken(LangParser::FOR, 0);
}

tree::TerminalNode* LangParser::ForStatementContext::OP() {
  return getToken(LangParser::OP, 0);
}

LangParser::ForControlContext* LangParser::ForStatementContext::forControl() {
  return getRuleContext<LangParser::ForControlContext>(0);
}

tree::TerminalNode* LangParser::ForStatementContext::CP() {
  return getToken(LangParser::CP, 0);
}

LangParser::BlockStatementContext* LangParser::ForStatementContext::blockStatement() {
  return getRuleContext<LangParser::BlockStatementContext>(0);
}


size_t LangParser::ForStatementContext::getRuleIndex() const {
  return LangParser::RuleForStatement;
}


std::any LangParser::ForStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForStatement(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ForStatementContext* LangParser::forStatement() {
  ForStatementContext *_localctx = _tracker.createInstance<ForStatementContext>(_ctx, getState());
  enterRule(_localctx, 102, LangParser::RuleForStatement);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(633);
    match(LangParser::FOR);
    setState(634);
    match(LangParser::OP);
    setState(635);
    forControl();
    setState(636);
    match(LangParser::CP);
    setState(637);
    blockStatement();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ForControlContext ------------------------------------------------------------------

LangParser::ForControlContext::ForControlContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::ForControlContext::getRuleIndex() const {
  return LangParser::RuleForControl;
}

void LangParser::ForControlContext::copyFrom(ForControlContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ForEachExplicitControlContext ------------------------------------------------------------------

std::vector<LangParser::Declaration_itemContext *> LangParser::ForEachExplicitControlContext::declaration_item() {
  return getRuleContexts<LangParser::Declaration_itemContext>();
}

LangParser::Declaration_itemContext* LangParser::ForEachExplicitControlContext::declaration_item(size_t i) {
  return getRuleContext<LangParser::Declaration_itemContext>(i);
}

tree::TerminalNode* LangParser::ForEachExplicitControlContext::COL() {
  return getToken(LangParser::COL, 0);
}

LangParser::ExpressionContext* LangParser::ForEachExplicitControlContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

std::vector<tree::TerminalNode *> LangParser::ForEachExplicitControlContext::COMMA() {
  return getTokens(LangParser::COMMA);
}

tree::TerminalNode* LangParser::ForEachExplicitControlContext::COMMA(size_t i) {
  return getToken(LangParser::COMMA, i);
}

LangParser::ForEachExplicitControlContext::ForEachExplicitControlContext(ForControlContext *ctx) { copyFrom(ctx); }


std::any LangParser::ForEachExplicitControlContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForEachExplicitControl(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ForCStyleControlContext ------------------------------------------------------------------

LangParser::ForInitStatementContext* LangParser::ForCStyleControlContext::forInitStatement() {
  return getRuleContext<LangParser::ForInitStatementContext>(0);
}

std::vector<tree::TerminalNode *> LangParser::ForCStyleControlContext::SEMICOLON() {
  return getTokens(LangParser::SEMICOLON);
}

tree::TerminalNode* LangParser::ForCStyleControlContext::SEMICOLON(size_t i) {
  return getToken(LangParser::SEMICOLON, i);
}

LangParser::ExpressionContext* LangParser::ForCStyleControlContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

LangParser::ForUpdateContext* LangParser::ForCStyleControlContext::forUpdate() {
  return getRuleContext<LangParser::ForUpdateContext>(0);
}

LangParser::ForCStyleControlContext::ForCStyleControlContext(ForControlContext *ctx) { copyFrom(ctx); }


std::any LangParser::ForCStyleControlContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForCStyleControl(this);
  else
    return visitor->visitChildren(this);
}
LangParser::ForControlContext* LangParser::forControl() {
  ForControlContext *_localctx = _tracker.createInstance<ForControlContext>(_ctx, getState());
  enterRule(_localctx, 104, LangParser::RuleForControl);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(659);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::ForCStyleControlContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(639);
      forInitStatement();
      setState(640);
      match(LangParser::SEMICOLON);
      setState(642);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 2485987080214153472) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 64)) & 31765) != 0)) {
        setState(641);
        expression();
      }
      setState(644);
      match(LangParser::SEMICOLON);
      setState(646);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 2485987080214153472) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 64)) & 31765) != 0)) {
        setState(645);
        forUpdate();
      }
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<LangParser::ForEachExplicitControlContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(648);
      declaration_item();
      setState(653);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == LangParser::COMMA) {
        setState(649);
        match(LangParser::COMMA);
        setState(650);
        declaration_item();
        setState(655);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(656);
      match(LangParser::COL);
      setState(657);
      expression();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ForUpdateContext ------------------------------------------------------------------

LangParser::ForUpdateContext::ForUpdateContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<LangParser::ForUpdateSingleContext *> LangParser::ForUpdateContext::forUpdateSingle() {
  return getRuleContexts<LangParser::ForUpdateSingleContext>();
}

LangParser::ForUpdateSingleContext* LangParser::ForUpdateContext::forUpdateSingle(size_t i) {
  return getRuleContext<LangParser::ForUpdateSingleContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::ForUpdateContext::COMMA() {
  return getTokens(LangParser::COMMA);
}

tree::TerminalNode* LangParser::ForUpdateContext::COMMA(size_t i) {
  return getToken(LangParser::COMMA, i);
}


size_t LangParser::ForUpdateContext::getRuleIndex() const {
  return LangParser::RuleForUpdate;
}


std::any LangParser::ForUpdateContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForUpdate(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ForUpdateContext* LangParser::forUpdate() {
  ForUpdateContext *_localctx = _tracker.createInstance<ForUpdateContext>(_ctx, getState());
  enterRule(_localctx, 106, LangParser::RuleForUpdate);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(661);
    forUpdateSingle();
    setState(666);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::COMMA) {
      setState(662);
      match(LangParser::COMMA);
      setState(663);
      forUpdateSingle();
      setState(668);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ForUpdateSingleContext ------------------------------------------------------------------

LangParser::ForUpdateSingleContext::ForUpdateSingleContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

LangParser::ExpressionContext* LangParser::ForUpdateSingleContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

LangParser::UpdateStatementContext* LangParser::ForUpdateSingleContext::updateStatement() {
  return getRuleContext<LangParser::UpdateStatementContext>(0);
}

LangParser::AssignStatementContext* LangParser::ForUpdateSingleContext::assignStatement() {
  return getRuleContext<LangParser::AssignStatementContext>(0);
}


size_t LangParser::ForUpdateSingleContext::getRuleIndex() const {
  return LangParser::RuleForUpdateSingle;
}


std::any LangParser::ForUpdateSingleContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForUpdateSingle(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ForUpdateSingleContext* LangParser::forUpdateSingle() {
  ForUpdateSingleContext *_localctx = _tracker.createInstance<ForUpdateSingleContext>(_ctx, getState());
  enterRule(_localctx, 108, LangParser::RuleForUpdateSingle);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(672);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(669);
      expression();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(670);
      updateStatement();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(671);
      assignStatement();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ForInitStatementContext ------------------------------------------------------------------

LangParser::ForInitStatementContext::ForInitStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

LangParser::MultiDeclarationContext* LangParser::ForInitStatementContext::multiDeclaration() {
  return getRuleContext<LangParser::MultiDeclarationContext>(0);
}

LangParser::AssignStatementContext* LangParser::ForInitStatementContext::assignStatement() {
  return getRuleContext<LangParser::AssignStatementContext>(0);
}

LangParser::ExpressionListContext* LangParser::ForInitStatementContext::expressionList() {
  return getRuleContext<LangParser::ExpressionListContext>(0);
}


size_t LangParser::ForInitStatementContext::getRuleIndex() const {
  return LangParser::RuleForInitStatement;
}


std::any LangParser::ForInitStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForInitStatement(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ForInitStatementContext* LangParser::forInitStatement() {
  ForInitStatementContext *_localctx = _tracker.createInstance<ForInitStatementContext>(_ctx, getState());
  enterRule(_localctx, 110, LangParser::RuleForInitStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(680);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(674);
      multiDeclaration();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(675);
      assignStatement();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(677);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 2485987080214153472) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 64)) & 31765) != 0)) {
        setState(676);
        expressionList();
      }
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);

      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MultiDeclarationContext ------------------------------------------------------------------

LangParser::MultiDeclarationContext::MultiDeclarationContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<LangParser::Declaration_itemContext *> LangParser::MultiDeclarationContext::declaration_item() {
  return getRuleContexts<LangParser::Declaration_itemContext>();
}

LangParser::Declaration_itemContext* LangParser::MultiDeclarationContext::declaration_item(size_t i) {
  return getRuleContext<LangParser::Declaration_itemContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::MultiDeclarationContext::ASSIGN() {
  return getTokens(LangParser::ASSIGN);
}

tree::TerminalNode* LangParser::MultiDeclarationContext::ASSIGN(size_t i) {
  return getToken(LangParser::ASSIGN, i);
}

std::vector<LangParser::ExpressionContext *> LangParser::MultiDeclarationContext::expression() {
  return getRuleContexts<LangParser::ExpressionContext>();
}

LangParser::ExpressionContext* LangParser::MultiDeclarationContext::expression(size_t i) {
  return getRuleContext<LangParser::ExpressionContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::MultiDeclarationContext::COMMA() {
  return getTokens(LangParser::COMMA);
}

tree::TerminalNode* LangParser::MultiDeclarationContext::COMMA(size_t i) {
  return getToken(LangParser::COMMA, i);
}


size_t LangParser::MultiDeclarationContext::getRuleIndex() const {
  return LangParser::RuleMultiDeclaration;
}


std::any LangParser::MultiDeclarationContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMultiDeclaration(this);
  else
    return visitor->visitChildren(this);
}

LangParser::MultiDeclarationContext* LangParser::multiDeclaration() {
  MultiDeclarationContext *_localctx = _tracker.createInstance<MultiDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 112, LangParser::RuleMultiDeclaration);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(682);
    declaration_item();
    setState(685);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::ASSIGN) {
      setState(683);
      match(LangParser::ASSIGN);
      setState(684);
      expression();
    }
    setState(695);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::COMMA) {
      setState(687);
      match(LangParser::COMMA);
      setState(688);
      declaration_item();
      setState(691);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::ASSIGN) {
        setState(689);
        match(LangParser::ASSIGN);
        setState(690);
        expression();
      }
      setState(697);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParameterListContext ------------------------------------------------------------------

LangParser::ParameterListContext::ParameterListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<LangParser::ParameterContext *> LangParser::ParameterListContext::parameter() {
  return getRuleContexts<LangParser::ParameterContext>();
}

LangParser::ParameterContext* LangParser::ParameterListContext::parameter(size_t i) {
  return getRuleContext<LangParser::ParameterContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::ParameterListContext::COMMA() {
  return getTokens(LangParser::COMMA);
}

tree::TerminalNode* LangParser::ParameterListContext::COMMA(size_t i) {
  return getToken(LangParser::COMMA, i);
}

tree::TerminalNode* LangParser::ParameterListContext::DDD() {
  return getToken(LangParser::DDD, 0);
}


size_t LangParser::ParameterListContext::getRuleIndex() const {
  return LangParser::RuleParameterList;
}


std::any LangParser::ParameterListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitParameterList(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ParameterListContext* LangParser::parameterList() {
  ParameterListContext *_localctx = _tracker.createInstance<ParameterListContext>(_ctx, getState());
  enterRule(_localctx, 114, LangParser::RuleParameterList);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    setState(711);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::INT:
      case LangParser::FLOAT:
      case LangParser::NUMBER:
      case LangParser::STRING:
      case LangParser::BOOL:
      case LangParser::ANY:
      case LangParser::VOID:
      case LangParser::NULL_:
      case LangParser::LIST:
      case LangParser::MAP:
      case LangParser::FUNCTION:
      case LangParser::COROUTINE:
      case LangParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(698);
        parameter();
        setState(703);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx);
        while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
          if (alt == 1) {
            setState(699);
            match(LangParser::COMMA);
            setState(700);
            parameter(); 
          }
          setState(705);
          _errHandler->sync(this);
          alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx);
        }
        setState(708);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::COMMA) {
          setState(706);
          match(LangParser::COMMA);
          setState(707);
          match(LangParser::DDD);
        }
        break;
      }

      case LangParser::DDD: {
        enterOuterAlt(_localctx, 2);
        setState(710);
        match(LangParser::DDD);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParameterContext ------------------------------------------------------------------

LangParser::ParameterContext::ParameterContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

LangParser::TypeContext* LangParser::ParameterContext::type() {
  return getRuleContext<LangParser::TypeContext>(0);
}

tree::TerminalNode* LangParser::ParameterContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}


size_t LangParser::ParameterContext::getRuleIndex() const {
  return LangParser::RuleParameter;
}


std::any LangParser::ParameterContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitParameter(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ParameterContext* LangParser::parameter() {
  ParameterContext *_localctx = _tracker.createInstance<ParameterContext>(_ctx, getState());
  enterRule(_localctx, 116, LangParser::RuleParameter);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(713);
    type();
    setState(714);
    match(LangParser::IDENTIFIER);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArgumentsContext ------------------------------------------------------------------

LangParser::ArgumentsContext::ArgumentsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

LangParser::ExpressionListContext* LangParser::ArgumentsContext::expressionList() {
  return getRuleContext<LangParser::ExpressionListContext>(0);
}


size_t LangParser::ArgumentsContext::getRuleIndex() const {
  return LangParser::RuleArguments;
}


std::any LangParser::ArgumentsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitArguments(this);
  else
    return visitor->visitChildren(this);
}

LangParser::ArgumentsContext* LangParser::arguments() {
  ArgumentsContext *_localctx = _tracker.createInstance<ArgumentsContext>(_ctx, getState());
  enterRule(_localctx, 118, LangParser::RuleArguments);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(717);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2485987080214153472) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 31765) != 0)) {
      setState(716);
      expressionList();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

void LangParser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  langparserParserInitialize();
#else
  ::antlr4::internal::call_once(langparserParserOnceFlag, langparserParserInitialize);
#endif
}
