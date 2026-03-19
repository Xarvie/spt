
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
      "importSpecifier", "deferStatement", "updateStatement", "assignStatement", 
      "lvalue", "lvalueSuffix", "declaration", "variableDeclaration", "declaration_item", 
      "functionDeclaration", "classDeclaration", "classMember", "type", 
      "qualifiedIdentifier", "primitiveType", "listType", "mapType", "expression", 
      "expressionList", "logicalOrExp", "logicalAndExp", "bitwiseOrExp", 
      "bitwiseXorExp", "bitwiseAndExp", "equalityExp", "equalityExpOp", 
      "comparisonExp", "comparisonExpOp", "shiftExp", "shiftExpOp", "concatExp", 
      "addSubExp", "addSubExpOp", "mulDivModExp", "mulDivModExpOp", "unaryExp", 
      "postfixExp", "postfixSuffix", "primaryExp", "atomexp", "lambdaExpression", 
      "listExpression", "mapExpression", "mapEntryList", "mapEntry", "ifStatement", 
      "whileStatement", "forStatement", "forControl", "forNumericVar", "forEachVar", 
      "parameterList", "parameter", "arguments"
    },
    std::vector<std::string>{
      "", "'int'", "'float'", "'number'", "'str'", "'bool'", "'any'", "'void'", 
      "'null'", "'list'", "'map'", "'function'", "'coro'", "'vars'", "'if'", 
      "'else'", "'while'", "'for'", "'break'", "'continue'", "'return'", 
      "'defer'", "'true'", "'false'", "'const'", "'auto'", "'global'", "'static'", 
      "'import'", "'as'", "'from'", "'private'", "'export'", "'class'", 
      "'+'", "'-'", "'*'", "'/'", "'~/'", "'%'", "'='", "'+='", "'-='", 
      "'*='", "'/='", "'~/='", "'%='", "'..='", "'=='", "'!='", "'<'", "'>'", 
      "'<='", "'>='", "'&&'", "'||'", "'!'", "'..'", "'#'", "'&'", "'|'", 
      "'^'", "'~'", "'<<'", "'->'", "'('", "')'", "'['", "']'", "'{'", "'}'", 
      "','", "'.'", "':'", "';'", "'...'"
    },
    std::vector<std::string>{
      "", "INT", "FLOAT", "NUMBER", "STR", "BOOL", "ANY", "VOID", "NULL_", 
      "LIST", "MAP", "FUNCTION", "COROUTINE", "VARS", "IF", "ELSE", "WHILE", 
      "FOR", "BREAK", "CONTINUE", "RETURN", "DEFER", "TRUE", "FALSE", "CONST", 
      "AUTO", "GLOBAL", "STATIC", "IMPORT", "AS", "FROM", "PRIVATE", "EXPORT", 
      "CLASS", "ADD", "SUB", "MUL", "DIV", "IDIV", "MOD", "ASSIGN", "ADD_ASSIGN", 
      "SUB_ASSIGN", "MUL_ASSIGN", "DIV_ASSIGN", "IDIV_ASSIGN", "MOD_ASSIGN", 
      "CONCAT_ASSIGN", "EQ", "NEQ", "LT", "GT", "LTE", "GTE", "AND", "OR", 
      "NOT", "CONCAT", "LEN", "BIT_AND", "BIT_OR", "BIT_XOR", "BIT_NOT", 
      "LSHIFT", "ARROW", "OP", "CP", "OSB", "CSB", "OCB", "CCB", "COMMA", 
      "DOT", "COL", "SEMICOLON", "DDD", "INTEGER", "FLOAT_LITERAL", "STRING_LITERAL", 
      "IDENTIFIER", "WS", "LINE_COMMENT", "BLOCK_COMMENT"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,82,703,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,2,56,7,
  	56,2,57,7,57,1,0,5,0,118,8,0,10,0,12,0,121,9,0,1,0,1,0,1,1,1,1,5,1,127,
  	8,1,10,1,12,1,130,9,1,1,1,1,1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,
  	1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,154,8,2,1,2,1,2,1,2,1,2,1,
  	2,1,2,3,2,162,8,2,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,1,3,5,3,175,
  	8,3,10,3,12,3,178,9,3,1,3,1,3,1,3,1,3,3,3,184,8,3,1,4,1,4,1,4,3,4,189,
  	8,4,1,5,1,5,1,5,1,6,1,6,1,6,1,6,1,7,1,7,1,7,5,7,201,8,7,10,7,12,7,204,
  	9,7,1,7,1,7,1,7,1,7,5,7,210,8,7,10,7,12,7,213,9,7,1,8,1,8,5,8,217,8,8,
  	10,8,12,8,220,9,8,1,9,1,9,1,9,1,9,1,9,1,9,3,9,228,8,9,1,10,3,10,231,8,
  	10,1,10,1,10,1,10,1,10,1,10,3,10,238,8,10,1,11,3,11,241,8,11,1,11,3,11,
  	244,8,11,1,11,1,11,1,11,3,11,249,8,11,1,11,1,11,3,11,253,8,11,1,11,3,
  	11,256,8,11,1,11,1,11,1,11,3,11,261,8,11,1,11,3,11,264,8,11,1,11,5,11,
  	267,8,11,10,11,12,11,270,9,11,1,11,1,11,3,11,274,8,11,3,11,276,8,11,1,
  	12,1,12,3,12,280,8,12,1,12,1,12,1,13,3,13,285,8,13,1,13,3,13,288,8,13,
  	1,13,1,13,1,13,1,13,3,13,294,8,13,1,13,1,13,1,13,1,13,3,13,300,8,13,1,
  	13,3,13,303,8,13,1,13,1,13,1,13,1,13,3,13,309,8,13,1,13,1,13,1,13,3,13,
  	314,8,13,1,14,1,14,1,14,1,14,5,14,320,8,14,10,14,12,14,323,9,14,1,14,
  	1,14,1,15,3,15,328,8,15,1,15,3,15,331,8,15,1,15,1,15,1,15,3,15,336,8,
  	15,1,15,3,15,339,8,15,1,15,3,15,342,8,15,1,15,1,15,1,15,1,15,3,15,348,
  	8,15,1,15,1,15,1,15,1,15,3,15,354,8,15,1,15,3,15,357,8,15,1,15,1,15,1,
  	15,1,15,3,15,363,8,15,1,15,1,15,1,15,3,15,368,8,15,1,16,1,16,1,16,1,16,
  	1,16,3,16,375,8,16,1,17,1,17,1,17,5,17,380,8,17,10,17,12,17,383,9,17,
  	1,18,1,18,1,19,1,19,1,19,1,19,1,19,3,19,392,8,19,1,20,1,20,1,20,1,20,
  	1,20,1,20,1,20,3,20,401,8,20,1,21,1,21,1,22,1,22,1,22,5,22,408,8,22,10,
  	22,12,22,411,9,22,1,23,1,23,1,23,5,23,416,8,23,10,23,12,23,419,9,23,1,
  	24,1,24,1,24,5,24,424,8,24,10,24,12,24,427,9,24,1,25,1,25,1,25,5,25,432,
  	8,25,10,25,12,25,435,9,25,1,26,1,26,1,26,5,26,440,8,26,10,26,12,26,443,
  	9,26,1,27,1,27,1,27,5,27,448,8,27,10,27,12,27,451,9,27,1,28,1,28,1,28,
  	1,28,5,28,457,8,28,10,28,12,28,460,9,28,1,29,1,29,1,30,1,30,1,30,1,30,
  	5,30,468,8,30,10,30,12,30,471,9,30,1,31,1,31,1,32,1,32,1,32,1,32,5,32,
  	479,8,32,10,32,12,32,482,9,32,1,33,1,33,1,33,3,33,487,8,33,1,34,1,34,
  	1,34,5,34,492,8,34,10,34,12,34,495,9,34,1,35,1,35,1,35,1,35,5,35,501,
  	8,35,10,35,12,35,504,9,35,1,36,1,36,1,37,1,37,1,37,1,37,5,37,512,8,37,
  	10,37,12,37,515,9,37,1,38,1,38,1,39,1,39,1,39,3,39,522,8,39,1,40,1,40,
  	5,40,526,8,40,10,40,12,40,529,9,40,1,41,1,41,1,41,1,41,1,41,1,41,1,41,
  	1,41,3,41,539,8,41,1,41,3,41,542,8,41,1,42,1,42,1,42,1,42,1,42,1,42,1,
  	42,1,42,1,42,1,42,3,42,554,8,42,1,43,1,43,1,44,1,44,1,44,3,44,561,8,44,
  	1,44,1,44,1,44,1,44,3,44,567,8,44,1,44,1,44,1,45,1,45,3,45,573,8,45,1,
  	45,1,45,1,46,1,46,3,46,579,8,46,1,46,1,46,1,47,1,47,1,47,5,47,586,8,47,
  	10,47,12,47,589,9,47,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,
  	1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,3,48,609,8,48,1,49,1,49,1,49,
  	1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,5,49,623,8,49,10,49,12,49,
  	626,9,49,1,49,1,49,3,49,630,8,49,1,50,1,50,1,50,1,50,1,50,1,50,1,51,1,
  	51,1,51,1,51,1,51,1,51,1,52,1,52,1,52,1,52,1,52,1,52,1,52,3,52,651,8,
  	52,1,52,1,52,1,52,5,52,656,8,52,10,52,12,52,659,9,52,1,52,1,52,1,52,3,
  	52,664,8,52,1,53,1,53,3,53,668,8,53,1,53,1,53,3,53,672,8,53,1,54,1,54,
  	3,54,676,8,54,1,54,1,54,3,54,680,8,54,1,55,1,55,1,55,5,55,685,8,55,10,
  	55,12,55,688,9,55,1,55,1,55,3,55,692,8,55,1,55,3,55,695,8,55,1,56,1,56,
  	1,56,1,57,3,57,701,8,57,1,57,0,0,58,0,2,4,6,8,10,12,14,16,18,20,22,24,
  	26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,70,
  	72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104,106,108,110,112,
  	114,0,8,1,0,41,47,3,0,1,5,7,8,11,12,1,0,48,49,1,0,50,53,1,0,34,35,1,0,
  	36,39,4,0,35,35,56,56,58,58,62,62,3,0,8,8,22,23,76,78,754,0,119,1,0,0,
  	0,2,124,1,0,0,0,4,161,1,0,0,0,6,183,1,0,0,0,8,185,1,0,0,0,10,190,1,0,
  	0,0,12,193,1,0,0,0,14,197,1,0,0,0,16,214,1,0,0,0,18,227,1,0,0,0,20,230,
  	1,0,0,0,22,275,1,0,0,0,24,279,1,0,0,0,26,313,1,0,0,0,28,315,1,0,0,0,30,
  	367,1,0,0,0,32,374,1,0,0,0,34,376,1,0,0,0,36,384,1,0,0,0,38,386,1,0,0,
  	0,40,393,1,0,0,0,42,402,1,0,0,0,44,404,1,0,0,0,46,412,1,0,0,0,48,420,
  	1,0,0,0,50,428,1,0,0,0,52,436,1,0,0,0,54,444,1,0,0,0,56,452,1,0,0,0,58,
  	461,1,0,0,0,60,463,1,0,0,0,62,472,1,0,0,0,64,474,1,0,0,0,66,486,1,0,0,
  	0,68,488,1,0,0,0,70,496,1,0,0,0,72,505,1,0,0,0,74,507,1,0,0,0,76,516,
  	1,0,0,0,78,521,1,0,0,0,80,523,1,0,0,0,82,541,1,0,0,0,84,553,1,0,0,0,86,
  	555,1,0,0,0,88,557,1,0,0,0,90,570,1,0,0,0,92,576,1,0,0,0,94,582,1,0,0,
  	0,96,608,1,0,0,0,98,610,1,0,0,0,100,631,1,0,0,0,102,637,1,0,0,0,104,663,
  	1,0,0,0,106,671,1,0,0,0,108,679,1,0,0,0,110,694,1,0,0,0,112,696,1,0,0,
  	0,114,700,1,0,0,0,116,118,3,4,2,0,117,116,1,0,0,0,118,121,1,0,0,0,119,
  	117,1,0,0,0,119,120,1,0,0,0,120,122,1,0,0,0,121,119,1,0,0,0,122,123,5,
  	0,0,1,123,1,1,0,0,0,124,128,5,69,0,0,125,127,3,4,2,0,126,125,1,0,0,0,
  	127,130,1,0,0,0,128,126,1,0,0,0,128,129,1,0,0,0,129,131,1,0,0,0,130,128,
  	1,0,0,0,131,132,5,70,0,0,132,3,1,0,0,0,133,162,5,74,0,0,134,135,3,14,
  	7,0,135,136,5,74,0,0,136,162,1,0,0,0,137,138,3,12,6,0,138,139,5,74,0,
  	0,139,162,1,0,0,0,140,141,3,42,21,0,141,142,5,74,0,0,142,162,1,0,0,0,
  	143,162,3,20,10,0,144,162,3,98,49,0,145,162,3,100,50,0,146,162,3,102,
  	51,0,147,148,5,18,0,0,148,162,5,74,0,0,149,150,5,19,0,0,150,162,5,74,
  	0,0,151,153,5,20,0,0,152,154,3,44,22,0,153,152,1,0,0,0,153,154,1,0,0,
  	0,154,155,1,0,0,0,155,162,5,74,0,0,156,162,3,2,1,0,157,158,3,6,3,0,158,
  	159,5,74,0,0,159,162,1,0,0,0,160,162,3,10,5,0,161,133,1,0,0,0,161,134,
  	1,0,0,0,161,137,1,0,0,0,161,140,1,0,0,0,161,143,1,0,0,0,161,144,1,0,0,
  	0,161,145,1,0,0,0,161,146,1,0,0,0,161,147,1,0,0,0,161,149,1,0,0,0,161,
  	151,1,0,0,0,161,156,1,0,0,0,161,157,1,0,0,0,161,160,1,0,0,0,162,5,1,0,
  	0,0,163,164,5,28,0,0,164,165,5,36,0,0,165,166,5,29,0,0,166,167,5,79,0,
  	0,167,168,5,30,0,0,168,184,5,78,0,0,169,170,5,28,0,0,170,171,5,69,0,0,
  	171,176,3,8,4,0,172,173,5,71,0,0,173,175,3,8,4,0,174,172,1,0,0,0,175,
  	178,1,0,0,0,176,174,1,0,0,0,176,177,1,0,0,0,177,179,1,0,0,0,178,176,1,
  	0,0,0,179,180,5,70,0,0,180,181,5,30,0,0,181,182,5,78,0,0,182,184,1,0,
  	0,0,183,163,1,0,0,0,183,169,1,0,0,0,184,7,1,0,0,0,185,188,5,79,0,0,186,
  	187,5,29,0,0,187,189,5,79,0,0,188,186,1,0,0,0,188,189,1,0,0,0,189,9,1,
  	0,0,0,190,191,5,21,0,0,191,192,3,2,1,0,192,11,1,0,0,0,193,194,3,16,8,
  	0,194,195,7,0,0,0,195,196,3,42,21,0,196,13,1,0,0,0,197,202,3,16,8,0,198,
  	199,5,71,0,0,199,201,3,16,8,0,200,198,1,0,0,0,201,204,1,0,0,0,202,200,
  	1,0,0,0,202,203,1,0,0,0,203,205,1,0,0,0,204,202,1,0,0,0,205,206,5,40,
  	0,0,206,211,3,42,21,0,207,208,5,71,0,0,208,210,3,42,21,0,209,207,1,0,
  	0,0,210,213,1,0,0,0,211,209,1,0,0,0,211,212,1,0,0,0,212,15,1,0,0,0,213,
  	211,1,0,0,0,214,218,5,79,0,0,215,217,3,18,9,0,216,215,1,0,0,0,217,220,
  	1,0,0,0,218,216,1,0,0,0,218,219,1,0,0,0,219,17,1,0,0,0,220,218,1,0,0,
  	0,221,222,5,67,0,0,222,223,3,42,21,0,223,224,5,68,0,0,224,228,1,0,0,0,
  	225,226,5,72,0,0,226,228,5,79,0,0,227,221,1,0,0,0,227,225,1,0,0,0,228,
  	19,1,0,0,0,229,231,5,32,0,0,230,229,1,0,0,0,230,231,1,0,0,0,231,237,1,
  	0,0,0,232,233,3,22,11,0,233,234,5,74,0,0,234,238,1,0,0,0,235,238,3,26,
  	13,0,236,238,3,28,14,0,237,232,1,0,0,0,237,235,1,0,0,0,237,236,1,0,0,
  	0,238,21,1,0,0,0,239,241,5,26,0,0,240,239,1,0,0,0,240,241,1,0,0,0,241,
  	243,1,0,0,0,242,244,5,24,0,0,243,242,1,0,0,0,243,244,1,0,0,0,244,245,
  	1,0,0,0,245,248,3,24,12,0,246,247,5,40,0,0,247,249,3,42,21,0,248,246,
  	1,0,0,0,248,249,1,0,0,0,249,276,1,0,0,0,250,252,5,13,0,0,251,253,5,26,
  	0,0,252,251,1,0,0,0,252,253,1,0,0,0,253,255,1,0,0,0,254,256,5,24,0,0,
  	255,254,1,0,0,0,255,256,1,0,0,0,256,257,1,0,0,0,257,268,5,79,0,0,258,
  	260,5,71,0,0,259,261,5,26,0,0,260,259,1,0,0,0,260,261,1,0,0,0,261,263,
  	1,0,0,0,262,264,5,24,0,0,263,262,1,0,0,0,263,264,1,0,0,0,264,265,1,0,
  	0,0,265,267,5,79,0,0,266,258,1,0,0,0,267,270,1,0,0,0,268,266,1,0,0,0,
  	268,269,1,0,0,0,269,273,1,0,0,0,270,268,1,0,0,0,271,272,5,40,0,0,272,
  	274,3,42,21,0,273,271,1,0,0,0,273,274,1,0,0,0,274,276,1,0,0,0,275,240,
  	1,0,0,0,275,250,1,0,0,0,276,23,1,0,0,0,277,280,3,32,16,0,278,280,5,25,
  	0,0,279,277,1,0,0,0,279,278,1,0,0,0,280,281,1,0,0,0,281,282,5,79,0,0,
  	282,25,1,0,0,0,283,285,5,26,0,0,284,283,1,0,0,0,284,285,1,0,0,0,285,287,
  	1,0,0,0,286,288,5,24,0,0,287,286,1,0,0,0,287,288,1,0,0,0,288,289,1,0,
  	0,0,289,290,3,32,16,0,290,291,3,34,17,0,291,293,5,65,0,0,292,294,3,110,
  	55,0,293,292,1,0,0,0,293,294,1,0,0,0,294,295,1,0,0,0,295,296,5,66,0,0,
  	296,297,3,2,1,0,297,314,1,0,0,0,298,300,5,26,0,0,299,298,1,0,0,0,299,
  	300,1,0,0,0,300,302,1,0,0,0,301,303,5,24,0,0,302,301,1,0,0,0,302,303,
  	1,0,0,0,303,304,1,0,0,0,304,305,5,13,0,0,305,306,3,34,17,0,306,308,5,
  	65,0,0,307,309,3,110,55,0,308,307,1,0,0,0,308,309,1,0,0,0,309,310,1,0,
  	0,0,310,311,5,66,0,0,311,312,3,2,1,0,312,314,1,0,0,0,313,284,1,0,0,0,
  	313,299,1,0,0,0,314,27,1,0,0,0,315,316,5,33,0,0,316,317,5,79,0,0,317,
  	321,5,69,0,0,318,320,3,30,15,0,319,318,1,0,0,0,320,323,1,0,0,0,321,319,
  	1,0,0,0,321,322,1,0,0,0,322,324,1,0,0,0,323,321,1,0,0,0,324,325,5,70,
  	0,0,325,29,1,0,0,0,326,328,5,27,0,0,327,326,1,0,0,0,327,328,1,0,0,0,328,
  	330,1,0,0,0,329,331,5,24,0,0,330,329,1,0,0,0,330,331,1,0,0,0,331,332,
  	1,0,0,0,332,335,3,24,12,0,333,334,5,40,0,0,334,336,3,42,21,0,335,333,
  	1,0,0,0,335,336,1,0,0,0,336,368,1,0,0,0,337,339,5,27,0,0,338,337,1,0,
  	0,0,338,339,1,0,0,0,339,341,1,0,0,0,340,342,5,24,0,0,341,340,1,0,0,0,
  	341,342,1,0,0,0,342,343,1,0,0,0,343,344,3,32,16,0,344,345,5,79,0,0,345,
  	347,5,65,0,0,346,348,3,110,55,0,347,346,1,0,0,0,347,348,1,0,0,0,348,349,
  	1,0,0,0,349,350,5,66,0,0,350,351,3,2,1,0,351,368,1,0,0,0,352,354,5,27,
  	0,0,353,352,1,0,0,0,353,354,1,0,0,0,354,356,1,0,0,0,355,357,5,24,0,0,
  	356,355,1,0,0,0,356,357,1,0,0,0,357,358,1,0,0,0,358,359,5,13,0,0,359,
  	360,5,79,0,0,360,362,5,65,0,0,361,363,3,110,55,0,362,361,1,0,0,0,362,
  	363,1,0,0,0,363,364,1,0,0,0,364,365,5,66,0,0,365,368,3,2,1,0,366,368,
  	5,74,0,0,367,327,1,0,0,0,367,338,1,0,0,0,367,353,1,0,0,0,367,366,1,0,
  	0,0,368,31,1,0,0,0,369,375,3,36,18,0,370,375,3,38,19,0,371,375,3,40,20,
  	0,372,375,5,6,0,0,373,375,3,34,17,0,374,369,1,0,0,0,374,370,1,0,0,0,374,
  	371,1,0,0,0,374,372,1,0,0,0,374,373,1,0,0,0,375,33,1,0,0,0,376,381,5,
  	79,0,0,377,378,5,72,0,0,378,380,5,79,0,0,379,377,1,0,0,0,380,383,1,0,
  	0,0,381,379,1,0,0,0,381,382,1,0,0,0,382,35,1,0,0,0,383,381,1,0,0,0,384,
  	385,7,1,0,0,385,37,1,0,0,0,386,391,5,9,0,0,387,388,5,50,0,0,388,389,3,
  	32,16,0,389,390,5,51,0,0,390,392,1,0,0,0,391,387,1,0,0,0,391,392,1,0,
  	0,0,392,39,1,0,0,0,393,400,5,10,0,0,394,395,5,50,0,0,395,396,3,32,16,
  	0,396,397,5,71,0,0,397,398,3,32,16,0,398,399,5,51,0,0,399,401,1,0,0,0,
  	400,394,1,0,0,0,400,401,1,0,0,0,401,41,1,0,0,0,402,403,3,46,23,0,403,
  	43,1,0,0,0,404,409,3,42,21,0,405,406,5,71,0,0,406,408,3,42,21,0,407,405,
  	1,0,0,0,408,411,1,0,0,0,409,407,1,0,0,0,409,410,1,0,0,0,410,45,1,0,0,
  	0,411,409,1,0,0,0,412,417,3,48,24,0,413,414,5,55,0,0,414,416,3,48,24,
  	0,415,413,1,0,0,0,416,419,1,0,0,0,417,415,1,0,0,0,417,418,1,0,0,0,418,
  	47,1,0,0,0,419,417,1,0,0,0,420,425,3,50,25,0,421,422,5,54,0,0,422,424,
  	3,50,25,0,423,421,1,0,0,0,424,427,1,0,0,0,425,423,1,0,0,0,425,426,1,0,
  	0,0,426,49,1,0,0,0,427,425,1,0,0,0,428,433,3,52,26,0,429,430,5,60,0,0,
  	430,432,3,52,26,0,431,429,1,0,0,0,432,435,1,0,0,0,433,431,1,0,0,0,433,
  	434,1,0,0,0,434,51,1,0,0,0,435,433,1,0,0,0,436,441,3,54,27,0,437,438,
  	5,61,0,0,438,440,3,54,27,0,439,437,1,0,0,0,440,443,1,0,0,0,441,439,1,
  	0,0,0,441,442,1,0,0,0,442,53,1,0,0,0,443,441,1,0,0,0,444,449,3,56,28,
  	0,445,446,5,59,0,0,446,448,3,56,28,0,447,445,1,0,0,0,448,451,1,0,0,0,
  	449,447,1,0,0,0,449,450,1,0,0,0,450,55,1,0,0,0,451,449,1,0,0,0,452,458,
  	3,60,30,0,453,454,3,58,29,0,454,455,3,60,30,0,455,457,1,0,0,0,456,453,
  	1,0,0,0,457,460,1,0,0,0,458,456,1,0,0,0,458,459,1,0,0,0,459,57,1,0,0,
  	0,460,458,1,0,0,0,461,462,7,2,0,0,462,59,1,0,0,0,463,469,3,64,32,0,464,
  	465,3,62,31,0,465,466,3,64,32,0,466,468,1,0,0,0,467,464,1,0,0,0,468,471,
  	1,0,0,0,469,467,1,0,0,0,469,470,1,0,0,0,470,61,1,0,0,0,471,469,1,0,0,
  	0,472,473,7,3,0,0,473,63,1,0,0,0,474,480,3,68,34,0,475,476,3,66,33,0,
  	476,477,3,68,34,0,477,479,1,0,0,0,478,475,1,0,0,0,479,482,1,0,0,0,480,
  	478,1,0,0,0,480,481,1,0,0,0,481,65,1,0,0,0,482,480,1,0,0,0,483,487,5,
  	63,0,0,484,485,5,51,0,0,485,487,5,51,0,0,486,483,1,0,0,0,486,484,1,0,
  	0,0,487,67,1,0,0,0,488,493,3,70,35,0,489,490,5,57,0,0,490,492,3,70,35,
  	0,491,489,1,0,0,0,492,495,1,0,0,0,493,491,1,0,0,0,493,494,1,0,0,0,494,
  	69,1,0,0,0,495,493,1,0,0,0,496,502,3,74,37,0,497,498,3,72,36,0,498,499,
  	3,74,37,0,499,501,1,0,0,0,500,497,1,0,0,0,501,504,1,0,0,0,502,500,1,0,
  	0,0,502,503,1,0,0,0,503,71,1,0,0,0,504,502,1,0,0,0,505,506,7,4,0,0,506,
  	73,1,0,0,0,507,513,3,78,39,0,508,509,3,76,38,0,509,510,3,78,39,0,510,
  	512,1,0,0,0,511,508,1,0,0,0,512,515,1,0,0,0,513,511,1,0,0,0,513,514,1,
  	0,0,0,514,75,1,0,0,0,515,513,1,0,0,0,516,517,7,5,0,0,517,77,1,0,0,0,518,
  	519,7,6,0,0,519,522,3,78,39,0,520,522,3,80,40,0,521,518,1,0,0,0,521,520,
  	1,0,0,0,522,79,1,0,0,0,523,527,3,84,42,0,524,526,3,82,41,0,525,524,1,
  	0,0,0,526,529,1,0,0,0,527,525,1,0,0,0,527,528,1,0,0,0,528,81,1,0,0,0,
  	529,527,1,0,0,0,530,531,5,67,0,0,531,532,3,42,21,0,532,533,5,68,0,0,533,
  	542,1,0,0,0,534,535,5,72,0,0,535,542,5,79,0,0,536,538,5,65,0,0,537,539,
  	3,114,57,0,538,537,1,0,0,0,538,539,1,0,0,0,539,540,1,0,0,0,540,542,5,
  	66,0,0,541,530,1,0,0,0,541,534,1,0,0,0,541,536,1,0,0,0,542,83,1,0,0,0,
  	543,554,3,86,43,0,544,554,3,90,45,0,545,554,3,92,46,0,546,554,5,79,0,
  	0,547,554,5,75,0,0,548,549,5,65,0,0,549,550,3,42,21,0,550,551,5,66,0,
  	0,551,554,1,0,0,0,552,554,3,88,44,0,553,543,1,0,0,0,553,544,1,0,0,0,553,
  	545,1,0,0,0,553,546,1,0,0,0,553,547,1,0,0,0,553,548,1,0,0,0,553,552,1,
  	0,0,0,554,85,1,0,0,0,555,556,7,7,0,0,556,87,1,0,0,0,557,558,5,11,0,0,
  	558,560,5,65,0,0,559,561,3,110,55,0,560,559,1,0,0,0,560,561,1,0,0,0,561,
  	562,1,0,0,0,562,563,5,66,0,0,563,566,5,64,0,0,564,567,3,32,16,0,565,567,
  	5,13,0,0,566,564,1,0,0,0,566,565,1,0,0,0,567,568,1,0,0,0,568,569,3,2,
  	1,0,569,89,1,0,0,0,570,572,5,67,0,0,571,573,3,44,22,0,572,571,1,0,0,0,
  	572,573,1,0,0,0,573,574,1,0,0,0,574,575,5,68,0,0,575,91,1,0,0,0,576,578,
  	5,69,0,0,577,579,3,94,47,0,578,577,1,0,0,0,578,579,1,0,0,0,579,580,1,
  	0,0,0,580,581,5,70,0,0,581,93,1,0,0,0,582,587,3,96,48,0,583,584,5,71,
  	0,0,584,586,3,96,48,0,585,583,1,0,0,0,586,589,1,0,0,0,587,585,1,0,0,0,
  	587,588,1,0,0,0,588,95,1,0,0,0,589,587,1,0,0,0,590,591,5,79,0,0,591,592,
  	5,73,0,0,592,609,3,42,21,0,593,594,5,67,0,0,594,595,3,42,21,0,595,596,
  	5,68,0,0,596,597,5,73,0,0,597,598,3,42,21,0,598,609,1,0,0,0,599,600,5,
  	78,0,0,600,601,5,73,0,0,601,609,3,42,21,0,602,603,5,76,0,0,603,604,5,
  	73,0,0,604,609,3,42,21,0,605,606,5,77,0,0,606,607,5,73,0,0,607,609,3,
  	42,21,0,608,590,1,0,0,0,608,593,1,0,0,0,608,599,1,0,0,0,608,602,1,0,0,
  	0,608,605,1,0,0,0,609,97,1,0,0,0,610,611,5,14,0,0,611,612,5,65,0,0,612,
  	613,3,42,21,0,613,614,5,66,0,0,614,624,3,2,1,0,615,616,5,15,0,0,616,617,
  	5,14,0,0,617,618,5,65,0,0,618,619,3,42,21,0,619,620,5,66,0,0,620,621,
  	3,2,1,0,621,623,1,0,0,0,622,615,1,0,0,0,623,626,1,0,0,0,624,622,1,0,0,
  	0,624,625,1,0,0,0,625,629,1,0,0,0,626,624,1,0,0,0,627,628,5,15,0,0,628,
  	630,3,2,1,0,629,627,1,0,0,0,629,630,1,0,0,0,630,99,1,0,0,0,631,632,5,
  	16,0,0,632,633,5,65,0,0,633,634,3,42,21,0,634,635,5,66,0,0,635,636,3,
  	2,1,0,636,101,1,0,0,0,637,638,5,17,0,0,638,639,5,65,0,0,639,640,3,104,
  	52,0,640,641,5,66,0,0,641,642,3,2,1,0,642,103,1,0,0,0,643,644,3,106,53,
  	0,644,645,5,40,0,0,645,646,3,42,21,0,646,647,5,71,0,0,647,650,3,42,21,
  	0,648,649,5,71,0,0,649,651,3,42,21,0,650,648,1,0,0,0,650,651,1,0,0,0,
  	651,664,1,0,0,0,652,657,3,108,54,0,653,654,5,71,0,0,654,656,3,108,54,
  	0,655,653,1,0,0,0,656,659,1,0,0,0,657,655,1,0,0,0,657,658,1,0,0,0,658,
  	660,1,0,0,0,659,657,1,0,0,0,660,661,5,73,0,0,661,662,3,44,22,0,662,664,
  	1,0,0,0,663,643,1,0,0,0,663,652,1,0,0,0,664,105,1,0,0,0,665,668,3,32,
  	16,0,666,668,5,25,0,0,667,665,1,0,0,0,667,666,1,0,0,0,668,669,1,0,0,0,
  	669,672,5,79,0,0,670,672,5,79,0,0,671,667,1,0,0,0,671,670,1,0,0,0,672,
  	107,1,0,0,0,673,676,3,32,16,0,674,676,5,25,0,0,675,673,1,0,0,0,675,674,
  	1,0,0,0,676,677,1,0,0,0,677,680,5,79,0,0,678,680,5,79,0,0,679,675,1,0,
  	0,0,679,678,1,0,0,0,680,109,1,0,0,0,681,686,3,112,56,0,682,683,5,71,0,
  	0,683,685,3,112,56,0,684,682,1,0,0,0,685,688,1,0,0,0,686,684,1,0,0,0,
  	686,687,1,0,0,0,687,691,1,0,0,0,688,686,1,0,0,0,689,690,5,71,0,0,690,
  	692,5,75,0,0,691,689,1,0,0,0,691,692,1,0,0,0,692,695,1,0,0,0,693,695,
  	5,75,0,0,694,681,1,0,0,0,694,693,1,0,0,0,695,111,1,0,0,0,696,697,3,32,
  	16,0,697,698,5,79,0,0,698,113,1,0,0,0,699,701,3,44,22,0,700,699,1,0,0,
  	0,700,701,1,0,0,0,701,115,1,0,0,0,83,119,128,153,161,176,183,188,202,
  	211,218,227,230,237,240,243,248,252,255,260,263,268,273,275,279,284,287,
  	293,299,302,308,313,321,327,330,335,338,341,347,353,356,362,367,374,381,
  	391,400,409,417,425,433,441,449,458,469,480,486,493,502,513,521,527,538,
  	541,553,560,566,572,578,587,608,624,629,650,657,663,667,671,675,679,686,
  	691,694,700
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
    setState(119);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 4971974036264288254) != 0) || ((((_la - 65) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 65)) & 32277) != 0)) {
      setState(116);
      statement();
      setState(121);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(122);
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
    setState(124);
    match(LangParser::OCB);
    setState(128);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 4971974036264288254) != 0) || ((((_la - 65) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 65)) & 32277) != 0)) {
      setState(125);
      statement();
      setState(130);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(131);
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
//----------------- DeferStmtContext ------------------------------------------------------------------

LangParser::DeferStatementContext* LangParser::DeferStmtContext::deferStatement() {
  return getRuleContext<LangParser::DeferStatementContext>(0);
}

LangParser::DeferStmtContext::DeferStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::DeferStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitDeferStmt(this);
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
    setState(161);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 3, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::SemicolonStmtContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(133);
      match(LangParser::SEMICOLON);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<LangParser::AssignStmtContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(134);
      assignStatement();
      setState(135);
      match(LangParser::SEMICOLON);
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<LangParser::UpdateStmtContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(137);
      updateStatement();
      setState(138);
      match(LangParser::SEMICOLON);
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<LangParser::ExpressionStmtContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(140);
      expression();
      setState(141);
      match(LangParser::SEMICOLON);
      break;
    }

    case 5: {
      _localctx = _tracker.createInstance<LangParser::DeclarationStmtContext>(_localctx);
      enterOuterAlt(_localctx, 5);
      setState(143);
      declaration();
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<LangParser::IfStmtContext>(_localctx);
      enterOuterAlt(_localctx, 6);
      setState(144);
      ifStatement();
      break;
    }

    case 7: {
      _localctx = _tracker.createInstance<LangParser::WhileStmtContext>(_localctx);
      enterOuterAlt(_localctx, 7);
      setState(145);
      whileStatement();
      break;
    }

    case 8: {
      _localctx = _tracker.createInstance<LangParser::ForStmtContext>(_localctx);
      enterOuterAlt(_localctx, 8);
      setState(146);
      forStatement();
      break;
    }

    case 9: {
      _localctx = _tracker.createInstance<LangParser::BreakStmtContext>(_localctx);
      enterOuterAlt(_localctx, 9);
      setState(147);
      match(LangParser::BREAK);
      setState(148);
      match(LangParser::SEMICOLON);
      break;
    }

    case 10: {
      _localctx = _tracker.createInstance<LangParser::ContinueStmtContext>(_localctx);
      enterOuterAlt(_localctx, 10);
      setState(149);
      match(LangParser::CONTINUE);
      setState(150);
      match(LangParser::SEMICOLON);
      break;
    }

    case 11: {
      _localctx = _tracker.createInstance<LangParser::ReturnStmtContext>(_localctx);
      enterOuterAlt(_localctx, 11);
      setState(151);
      match(LangParser::RETURN);
      setState(153);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 4971974022989351168) != 0) || ((((_la - 65) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 65)) & 31765) != 0)) {
        setState(152);
        expressionList();
      }
      setState(155);
      match(LangParser::SEMICOLON);
      break;
    }

    case 12: {
      _localctx = _tracker.createInstance<LangParser::BlockStmtContext>(_localctx);
      enterOuterAlt(_localctx, 12);
      setState(156);
      blockStatement();
      break;
    }

    case 13: {
      _localctx = _tracker.createInstance<LangParser::ImportStmtContext>(_localctx);
      enterOuterAlt(_localctx, 13);
      setState(157);
      importStatement();
      setState(158);
      match(LangParser::SEMICOLON);
      break;
    }

    case 14: {
      _localctx = _tracker.createInstance<LangParser::DeferStmtContext>(_localctx);
      enterOuterAlt(_localctx, 14);
      setState(160);
      deferStatement();
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
    setState(183);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 5, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::ImportNamespaceStmtContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(163);
      match(LangParser::IMPORT);
      setState(164);
      match(LangParser::MUL);
      setState(165);
      match(LangParser::AS);
      setState(166);
      match(LangParser::IDENTIFIER);
      setState(167);
      match(LangParser::FROM);
      setState(168);
      match(LangParser::STRING_LITERAL);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<LangParser::ImportNamedStmtContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(169);
      match(LangParser::IMPORT);
      setState(170);
      match(LangParser::OCB);
      setState(171);
      importSpecifier();
      setState(176);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == LangParser::COMMA) {
        setState(172);
        match(LangParser::COMMA);
        setState(173);
        importSpecifier();
        setState(178);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(179);
      match(LangParser::CCB);
      setState(180);
      match(LangParser::FROM);
      setState(181);
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
    setState(185);
    match(LangParser::IDENTIFIER);
    setState(188);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::AS) {
      setState(186);
      match(LangParser::AS);
      setState(187);
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

//----------------- DeferStatementContext ------------------------------------------------------------------

LangParser::DeferStatementContext::DeferStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::DeferStatementContext::getRuleIndex() const {
  return LangParser::RuleDeferStatement;
}

void LangParser::DeferStatementContext::copyFrom(DeferStatementContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- DeferBlockStmtContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::DeferBlockStmtContext::DEFER() {
  return getToken(LangParser::DEFER, 0);
}

LangParser::BlockStatementContext* LangParser::DeferBlockStmtContext::blockStatement() {
  return getRuleContext<LangParser::BlockStatementContext>(0);
}

LangParser::DeferBlockStmtContext::DeferBlockStmtContext(DeferStatementContext *ctx) { copyFrom(ctx); }


std::any LangParser::DeferBlockStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitDeferBlockStmt(this);
  else
    return visitor->visitChildren(this);
}
LangParser::DeferStatementContext* LangParser::deferStatement() {
  DeferStatementContext *_localctx = _tracker.createInstance<DeferStatementContext>(_ctx, getState());
  enterRule(_localctx, 10, LangParser::RuleDeferStatement);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::DeferBlockStmtContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(190);
    match(LangParser::DEFER);
    setState(191);
    blockStatement();
   
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

tree::TerminalNode* LangParser::UpdateAssignStmtContext::IDIV_ASSIGN() {
  return getToken(LangParser::IDIV_ASSIGN, 0);
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
  enterRule(_localctx, 12, LangParser::RuleUpdateStatement);
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
    setState(193);
    lvalue();
    setState(194);
    antlrcpp::downCast<UpdateAssignStmtContext *>(_localctx)->op = _input->LT(1);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 279275953455104) != 0))) {
      antlrcpp::downCast<UpdateAssignStmtContext *>(_localctx)->op = _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(195);
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
  enterRule(_localctx, 14, LangParser::RuleAssignStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    _localctx = _tracker.createInstance<LangParser::NormalAssignStmtContext>(_localctx);
    enterOuterAlt(_localctx, 1);
    setState(197);
    lvalue();
    setState(202);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::COMMA) {
      setState(198);
      match(LangParser::COMMA);
      setState(199);
      lvalue();
      setState(204);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(205);
    match(LangParser::ASSIGN);
    setState(206);
    expression();
    setState(211);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::COMMA) {
      setState(207);
      match(LangParser::COMMA);
      setState(208);
      expression();
      setState(213);
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
  enterRule(_localctx, 16, LangParser::RuleLvalue);
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
    setState(214);
    match(LangParser::IDENTIFIER);
    setState(218);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::OSB

    || _la == LangParser::DOT) {
      setState(215);
      lvalueSuffix();
      setState(220);
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
  enterRule(_localctx, 18, LangParser::RuleLvalueSuffix);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(227);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::OSB: {
        _localctx = _tracker.createInstance<LangParser::LvalueIndexContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(221);
        match(LangParser::OSB);
        setState(222);
        expression();
        setState(223);
        match(LangParser::CSB);
        break;
      }

      case LangParser::DOT: {
        _localctx = _tracker.createInstance<LangParser::LvalueMemberContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(225);
        match(LangParser::DOT);
        setState(226);
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
  enterRule(_localctx, 20, LangParser::RuleDeclaration);
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
    setState(230);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::EXPORT) {
      setState(229);
      match(LangParser::EXPORT);
    }
    setState(237);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 12, _ctx)) {
    case 1: {
      setState(232);
      variableDeclaration();
      setState(233);
      match(LangParser::SEMICOLON);
      break;
    }

    case 2: {
      setState(235);
      functionDeclaration();
      break;
    }

    case 3: {
      setState(236);
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

tree::TerminalNode* LangParser::MutiVariableDeclarationDefContext::VARS() {
  return getToken(LangParser::VARS, 0);
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
  enterRule(_localctx, 22, LangParser::RuleVariableDeclaration);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(275);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::INT:
      case LangParser::FLOAT:
      case LangParser::NUMBER:
      case LangParser::STR:
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
        setState(240);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::GLOBAL) {
          setState(239);
          match(LangParser::GLOBAL);
        }
        setState(243);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::CONST) {
          setState(242);
          match(LangParser::CONST);
        }
        setState(245);
        declaration_item();
        setState(248);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::ASSIGN) {
          setState(246);
          match(LangParser::ASSIGN);
          setState(247);
          expression();
        }
        break;
      }

      case LangParser::VARS: {
        _localctx = _tracker.createInstance<LangParser::MutiVariableDeclarationDefContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(250);
        match(LangParser::VARS);
        setState(252);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::GLOBAL) {
          setState(251);
          match(LangParser::GLOBAL);
        }
        setState(255);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::CONST) {
          setState(254);
          match(LangParser::CONST);
        }
        setState(257);
        match(LangParser::IDENTIFIER);
        setState(268);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == LangParser::COMMA) {
          setState(258);
          match(LangParser::COMMA);
          setState(260);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == LangParser::GLOBAL) {
            setState(259);
            match(LangParser::GLOBAL);
          }
          setState(263);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == LangParser::CONST) {
            setState(262);
            match(LangParser::CONST);
          }
          setState(265);
          match(LangParser::IDENTIFIER);
          setState(270);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        setState(273);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::ASSIGN) {
          setState(271);
          match(LangParser::ASSIGN);
          setState(272);
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
  enterRule(_localctx, 24, LangParser::RuleDeclaration_item);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(279);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::INT:
      case LangParser::FLOAT:
      case LangParser::NUMBER:
      case LangParser::STR:
      case LangParser::BOOL:
      case LangParser::ANY:
      case LangParser::VOID:
      case LangParser::NULL_:
      case LangParser::LIST:
      case LangParser::MAP:
      case LangParser::FUNCTION:
      case LangParser::COROUTINE:
      case LangParser::IDENTIFIER: {
        setState(277);
        type();
        break;
      }

      case LangParser::AUTO: {
        setState(278);
        match(LangParser::AUTO);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(281);
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

tree::TerminalNode* LangParser::FunctionDeclarationDefContext::CONST() {
  return getToken(LangParser::CONST, 0);
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

tree::TerminalNode* LangParser::MultiReturnFunctionDeclarationDefContext::VARS() {
  return getToken(LangParser::VARS, 0);
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

tree::TerminalNode* LangParser::MultiReturnFunctionDeclarationDefContext::CONST() {
  return getToken(LangParser::CONST, 0);
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
  enterRule(_localctx, 26, LangParser::RuleFunctionDeclaration);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(313);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::FunctionDeclarationDefContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(284);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::GLOBAL) {
        setState(283);
        match(LangParser::GLOBAL);
      }
      setState(287);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::CONST) {
        setState(286);
        match(LangParser::CONST);
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
      setState(302);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::CONST) {
        setState(301);
        match(LangParser::CONST);
      }
      setState(304);
      match(LangParser::VARS);
      setState(305);
      qualifiedIdentifier();
      setState(306);
      match(LangParser::OP);
      setState(308);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8190) != 0) || _la == LangParser::DDD

      || _la == LangParser::IDENTIFIER) {
        setState(307);
        parameterList();
      }
      setState(310);
      match(LangParser::CP);
      setState(311);
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
  enterRule(_localctx, 28, LangParser::RuleClassDeclaration);
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
    setState(315);
    match(LangParser::CLASS);
    setState(316);
    match(LangParser::IDENTIFIER);
    setState(317);
    match(LangParser::OCB);
    setState(321);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 184565758) != 0) || _la == LangParser::SEMICOLON

    || _la == LangParser::IDENTIFIER) {
      setState(318);
      classMember();
      setState(323);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(324);
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

tree::TerminalNode* LangParser::ClassMethodMemberContext::CONST() {
  return getToken(LangParser::CONST, 0);
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

tree::TerminalNode* LangParser::MultiReturnClassMethodMemberContext::VARS() {
  return getToken(LangParser::VARS, 0);
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

tree::TerminalNode* LangParser::MultiReturnClassMethodMemberContext::CONST() {
  return getToken(LangParser::CONST, 0);
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
  enterRule(_localctx, 30, LangParser::RuleClassMember);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(367);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 41, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::ClassFieldMemberContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(327);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::STATIC) {
        setState(326);
        match(LangParser::STATIC);
      }
      setState(330);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::CONST) {
        setState(329);
        match(LangParser::CONST);
      }
      setState(332);
      declaration_item();
      setState(335);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::ASSIGN) {
        setState(333);
        match(LangParser::ASSIGN);
        setState(334);
        expression();
      }
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<LangParser::ClassMethodMemberContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(338);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::STATIC) {
        setState(337);
        match(LangParser::STATIC);
      }
      setState(341);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::CONST) {
        setState(340);
        match(LangParser::CONST);
      }
      setState(343);
      type();
      setState(344);
      match(LangParser::IDENTIFIER);
      setState(345);
      match(LangParser::OP);
      setState(347);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8190) != 0) || _la == LangParser::DDD

      || _la == LangParser::IDENTIFIER) {
        setState(346);
        parameterList();
      }
      setState(349);
      match(LangParser::CP);
      setState(350);
      blockStatement();
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<LangParser::MultiReturnClassMethodMemberContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(353);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::STATIC) {
        setState(352);
        match(LangParser::STATIC);
      }
      setState(356);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::CONST) {
        setState(355);
        match(LangParser::CONST);
      }
      setState(358);
      match(LangParser::VARS);
      setState(359);
      match(LangParser::IDENTIFIER);
      setState(360);
      match(LangParser::OP);
      setState(362);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8190) != 0) || _la == LangParser::DDD

      || _la == LangParser::IDENTIFIER) {
        setState(361);
        parameterList();
      }
      setState(364);
      match(LangParser::CP);
      setState(365);
      blockStatement();
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<LangParser::ClassEmptyMemberContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(366);
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
  enterRule(_localctx, 32, LangParser::RuleType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(374);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::INT:
      case LangParser::FLOAT:
      case LangParser::NUMBER:
      case LangParser::STR:
      case LangParser::BOOL:
      case LangParser::VOID:
      case LangParser::NULL_:
      case LangParser::FUNCTION:
      case LangParser::COROUTINE: {
        _localctx = _tracker.createInstance<LangParser::TypePrimitiveContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(369);
        primitiveType();
        break;
      }

      case LangParser::LIST: {
        _localctx = _tracker.createInstance<LangParser::TypeListTypeContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(370);
        listType();
        break;
      }

      case LangParser::MAP: {
        _localctx = _tracker.createInstance<LangParser::TypeMapContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(371);
        mapType();
        break;
      }

      case LangParser::ANY: {
        _localctx = _tracker.createInstance<LangParser::TypeAnyContext>(_localctx);
        enterOuterAlt(_localctx, 4);
        setState(372);
        match(LangParser::ANY);
        break;
      }

      case LangParser::IDENTIFIER: {
        _localctx = _tracker.createInstance<LangParser::TypeQualifiedIdentifierContext>(_localctx);
        enterOuterAlt(_localctx, 5);
        setState(373);
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
  enterRule(_localctx, 34, LangParser::RuleQualifiedIdentifier);
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
    setState(376);
    match(LangParser::IDENTIFIER);
    setState(381);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::DOT) {
      setState(377);
      match(LangParser::DOT);
      setState(378);
      match(LangParser::IDENTIFIER);
      setState(383);
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

tree::TerminalNode* LangParser::PrimitiveTypeContext::STR() {
  return getToken(LangParser::STR, 0);
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
  enterRule(_localctx, 36, LangParser::RulePrimitiveType);
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
  enterRule(_localctx, 38, LangParser::RuleListType);
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
    setState(386);
    match(LangParser::LIST);
    setState(391);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::LT) {
      setState(387);
      match(LangParser::LT);
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
  enterRule(_localctx, 40, LangParser::RuleMapType);
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
    setState(393);
    match(LangParser::MAP);
    setState(400);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::LT) {
      setState(394);
      match(LangParser::LT);
      setState(395);
      type();
      setState(396);
      match(LangParser::COMMA);
      setState(397);
      type();
      setState(398);
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
  enterRule(_localctx, 42, LangParser::RuleExpression);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(402);
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
  enterRule(_localctx, 44, LangParser::RuleExpressionList);
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
    setState(404);
    expression();
    setState(409);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::COMMA) {
      setState(405);
      match(LangParser::COMMA);
      setState(406);
      expression();
      setState(411);
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
  enterRule(_localctx, 46, LangParser::RuleLogicalOrExp);
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
    setState(412);
    logicalAndExp();
    setState(417);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::OR) {
      setState(413);
      match(LangParser::OR);
      setState(414);
      logicalAndExp();
      setState(419);
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
  enterRule(_localctx, 48, LangParser::RuleLogicalAndExp);
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
    setState(420);
    bitwiseOrExp();
    setState(425);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::AND) {
      setState(421);
      match(LangParser::AND);
      setState(422);
      bitwiseOrExp();
      setState(427);
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
  enterRule(_localctx, 50, LangParser::RuleBitwiseOrExp);
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
    setState(428);
    bitwiseXorExp();
    setState(433);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::BIT_OR) {
      setState(429);
      match(LangParser::BIT_OR);
      setState(430);
      bitwiseXorExp();
      setState(435);
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
  enterRule(_localctx, 52, LangParser::RuleBitwiseXorExp);
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
    setState(436);
    bitwiseAndExp();
    setState(441);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::BIT_XOR) {
      setState(437);
      match(LangParser::BIT_XOR);
      setState(438);
      bitwiseAndExp();
      setState(443);
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
  enterRule(_localctx, 54, LangParser::RuleBitwiseAndExp);
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
    setState(444);
    equalityExp();
    setState(449);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::BIT_AND) {
      setState(445);
      match(LangParser::BIT_AND);
      setState(446);
      equalityExp();
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
  enterRule(_localctx, 56, LangParser::RuleEqualityExp);
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
    setState(452);
    comparisonExp();
    setState(458);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::EQ

    || _la == LangParser::NEQ) {
      setState(453);
      equalityExpOp();
      setState(454);
      comparisonExp();
      setState(460);
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
  enterRule(_localctx, 58, LangParser::RuleEqualityExpOp);
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
    setState(461);
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
  enterRule(_localctx, 60, LangParser::RuleComparisonExp);
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
    setState(463);
    shiftExp();
    setState(469);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 16888498602639360) != 0)) {
      setState(464);
      comparisonExpOp();
      setState(465);
      shiftExp();
      setState(471);
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
  enterRule(_localctx, 62, LangParser::RuleComparisonExpOp);
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
    setState(472);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 16888498602639360) != 0))) {
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
  enterRule(_localctx, 64, LangParser::RuleShiftExp);

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
    setState(474);
    concatExp();
    setState(480);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 54, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(475);
        shiftExpOp();
        setState(476);
        concatExp(); 
      }
      setState(482);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 54, _ctx);
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
  enterRule(_localctx, 66, LangParser::RuleShiftExpOp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(486);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::LSHIFT: {
        setState(483);
        match(LangParser::LSHIFT);
        break;
      }

      case LangParser::GT: {
        setState(484);
        match(LangParser::GT);
        setState(485);
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
  enterRule(_localctx, 68, LangParser::RuleConcatExp);
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
    setState(488);
    addSubExp();
    setState(493);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::CONCAT) {
      setState(489);
      match(LangParser::CONCAT);
      setState(490);
      addSubExp();
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
  enterRule(_localctx, 70, LangParser::RuleAddSubExp);
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
    setState(496);
    mulDivModExp();
    setState(502);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::ADD

    || _la == LangParser::SUB) {
      setState(497);
      addSubExpOp();
      setState(498);
      mulDivModExp();
      setState(504);
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
  enterRule(_localctx, 72, LangParser::RuleAddSubExpOp);
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
    setState(505);
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
  enterRule(_localctx, 74, LangParser::RuleMulDivModExp);
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
    setState(507);
    unaryExp();
    setState(513);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1030792151040) != 0)) {
      setState(508);
      mulDivModExpOp();
      setState(509);
      unaryExp();
      setState(515);
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

tree::TerminalNode* LangParser::MulDivModExpOpContext::IDIV() {
  return getToken(LangParser::IDIV, 0);
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
  enterRule(_localctx, 76, LangParser::RuleMulDivModExpOp);
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
    setState(516);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1030792151040) != 0))) {
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
  enterRule(_localctx, 78, LangParser::RuleUnaryExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(521);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::SUB:
      case LangParser::NOT:
      case LangParser::LEN:
      case LangParser::BIT_NOT: {
        _localctx = _tracker.createInstance<LangParser::UnaryPrefixContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(518);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 4971974022976765952) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(519);
        unaryExp();
        break;
      }

      case LangParser::NULL_:
      case LangParser::FUNCTION:
      case LangParser::TRUE:
      case LangParser::FALSE:
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
        setState(520);
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
  enterRule(_localctx, 80, LangParser::RulePostfixExp);
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
    setState(523);
    primaryExp();
    setState(527);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (((((_la - 65) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 65)) & 133) != 0)) {
      setState(524);
      postfixSuffix();
      setState(529);
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
  enterRule(_localctx, 82, LangParser::RulePostfixSuffix);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(541);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::OSB: {
        _localctx = _tracker.createInstance<LangParser::PostfixIndexSuffixContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(530);
        match(LangParser::OSB);
        setState(531);
        expression();
        setState(532);
        match(LangParser::CSB);
        break;
      }

      case LangParser::DOT: {
        _localctx = _tracker.createInstance<LangParser::PostfixMemberSuffixContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(534);
        match(LangParser::DOT);
        setState(535);
        match(LangParser::IDENTIFIER);
        break;
      }

      case LangParser::OP: {
        _localctx = _tracker.createInstance<LangParser::PostfixCallSuffixContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(536);
        match(LangParser::OP);
        setState(538);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx)) {
        case 1: {
          setState(537);
          arguments();
          break;
        }

        default:
          break;
        }
        setState(540);
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
  enterRule(_localctx, 84, LangParser::RulePrimaryExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(553);
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
        setState(543);
        atomexp();
        break;
      }

      case LangParser::OSB: {
        _localctx = _tracker.createInstance<LangParser::PrimaryListLiteralContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(544);
        listExpression();
        break;
      }

      case LangParser::OCB: {
        _localctx = _tracker.createInstance<LangParser::PrimaryMapLiteralContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(545);
        mapExpression();
        break;
      }

      case LangParser::IDENTIFIER: {
        _localctx = _tracker.createInstance<LangParser::PrimaryIdentifierContext>(_localctx);
        enterOuterAlt(_localctx, 4);
        setState(546);
        match(LangParser::IDENTIFIER);
        break;
      }

      case LangParser::DDD: {
        _localctx = _tracker.createInstance<LangParser::PrimaryVarArgsContext>(_localctx);
        enterOuterAlt(_localctx, 5);
        setState(547);
        match(LangParser::DDD);
        break;
      }

      case LangParser::OP: {
        _localctx = _tracker.createInstance<LangParser::PrimaryParenExpContext>(_localctx);
        enterOuterAlt(_localctx, 6);
        setState(548);
        match(LangParser::OP);
        setState(549);
        expression();
        setState(550);
        match(LangParser::CP);
        break;
      }

      case LangParser::FUNCTION: {
        _localctx = _tracker.createInstance<LangParser::PrimaryLambdaContext>(_localctx);
        enterOuterAlt(_localctx, 7);
        setState(552);
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
  enterRule(_localctx, 86, LangParser::RuleAtomexp);
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
    setState(555);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 12583168) != 0) || ((((_la - 76) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 76)) & 7) != 0))) {
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

tree::TerminalNode* LangParser::LambdaExprDefContext::VARS() {
  return getToken(LangParser::VARS, 0);
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
  enterRule(_localctx, 88, LangParser::RuleLambdaExpression);
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
    setState(557);
    match(LangParser::FUNCTION);
    setState(558);
    match(LangParser::OP);
    setState(560);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8190) != 0) || _la == LangParser::DDD

    || _la == LangParser::IDENTIFIER) {
      setState(559);
      parameterList();
    }
    setState(562);
    match(LangParser::CP);
    setState(563);
    match(LangParser::ARROW);
    setState(566);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::INT:
      case LangParser::FLOAT:
      case LangParser::NUMBER:
      case LangParser::STR:
      case LangParser::BOOL:
      case LangParser::ANY:
      case LangParser::VOID:
      case LangParser::NULL_:
      case LangParser::LIST:
      case LangParser::MAP:
      case LangParser::FUNCTION:
      case LangParser::COROUTINE:
      case LangParser::IDENTIFIER: {
        setState(564);
        type();
        break;
      }

      case LangParser::VARS: {
        setState(565);
        match(LangParser::VARS);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(568);
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
  enterRule(_localctx, 90, LangParser::RuleListExpression);
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
    setState(570);
    match(LangParser::OSB);
    setState(572);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 4971974022989351168) != 0) || ((((_la - 65) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 65)) & 31765) != 0)) {
      setState(571);
      expressionList();
    }
    setState(574);
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
  enterRule(_localctx, 92, LangParser::RuleMapExpression);
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
    setState(576);
    match(LangParser::OCB);
    setState(578);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 67) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 67)) & 7681) != 0)) {
      setState(577);
      mapEntryList();
    }
    setState(580);
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
  enterRule(_localctx, 94, LangParser::RuleMapEntryList);
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
    setState(582);
    mapEntry();
    setState(587);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == LangParser::COMMA) {
      setState(583);
      match(LangParser::COMMA);
      setState(584);
      mapEntry();
      setState(589);
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
//----------------- MapEntryIntKeyContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::MapEntryIntKeyContext::INTEGER() {
  return getToken(LangParser::INTEGER, 0);
}

tree::TerminalNode* LangParser::MapEntryIntKeyContext::COL() {
  return getToken(LangParser::COL, 0);
}

LangParser::ExpressionContext* LangParser::MapEntryIntKeyContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

LangParser::MapEntryIntKeyContext::MapEntryIntKeyContext(MapEntryContext *ctx) { copyFrom(ctx); }


std::any LangParser::MapEntryIntKeyContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMapEntryIntKey(this);
  else
    return visitor->visitChildren(this);
}
//----------------- MapEntryFloatKeyContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::MapEntryFloatKeyContext::FLOAT_LITERAL() {
  return getToken(LangParser::FLOAT_LITERAL, 0);
}

tree::TerminalNode* LangParser::MapEntryFloatKeyContext::COL() {
  return getToken(LangParser::COL, 0);
}

LangParser::ExpressionContext* LangParser::MapEntryFloatKeyContext::expression() {
  return getRuleContext<LangParser::ExpressionContext>(0);
}

LangParser::MapEntryFloatKeyContext::MapEntryFloatKeyContext(MapEntryContext *ctx) { copyFrom(ctx); }


std::any LangParser::MapEntryFloatKeyContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitMapEntryFloatKey(this);
  else
    return visitor->visitChildren(this);
}
LangParser::MapEntryContext* LangParser::mapEntry() {
  MapEntryContext *_localctx = _tracker.createInstance<MapEntryContext>(_ctx, getState());
  enterRule(_localctx, 96, LangParser::RuleMapEntry);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(608);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::IDENTIFIER: {
        _localctx = _tracker.createInstance<LangParser::MapEntryIdentKeyContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(590);
        match(LangParser::IDENTIFIER);
        setState(591);
        match(LangParser::COL);
        setState(592);
        expression();
        break;
      }

      case LangParser::OSB: {
        _localctx = _tracker.createInstance<LangParser::MapEntryExprKeyContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(593);
        match(LangParser::OSB);
        setState(594);
        expression();
        setState(595);
        match(LangParser::CSB);
        setState(596);
        match(LangParser::COL);
        setState(597);
        expression();
        break;
      }

      case LangParser::STRING_LITERAL: {
        _localctx = _tracker.createInstance<LangParser::MapEntryStringKeyContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(599);
        match(LangParser::STRING_LITERAL);
        setState(600);
        match(LangParser::COL);
        setState(601);
        expression();
        break;
      }

      case LangParser::INTEGER: {
        _localctx = _tracker.createInstance<LangParser::MapEntryIntKeyContext>(_localctx);
        enterOuterAlt(_localctx, 4);
        setState(602);
        match(LangParser::INTEGER);
        setState(603);
        match(LangParser::COL);
        setState(604);
        expression();
        break;
      }

      case LangParser::FLOAT_LITERAL: {
        _localctx = _tracker.createInstance<LangParser::MapEntryFloatKeyContext>(_localctx);
        enterOuterAlt(_localctx, 5);
        setState(605);
        match(LangParser::FLOAT_LITERAL);
        setState(606);
        match(LangParser::COL);
        setState(607);
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
    setState(610);
    match(LangParser::IF);
    setState(611);
    match(LangParser::OP);
    setState(612);
    expression();
    setState(613);
    match(LangParser::CP);
    setState(614);
    blockStatement();
    setState(624);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(615);
        match(LangParser::ELSE);
        setState(616);
        match(LangParser::IF);
        setState(617);
        match(LangParser::OP);
        setState(618);
        expression();
        setState(619);
        match(LangParser::CP);
        setState(620);
        blockStatement(); 
      }
      setState(626);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx);
    }
    setState(629);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == LangParser::ELSE) {
      setState(627);
      match(LangParser::ELSE);
      setState(628);
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
    setState(631);
    match(LangParser::WHILE);
    setState(632);
    match(LangParser::OP);
    setState(633);
    expression();
    setState(634);
    match(LangParser::CP);
    setState(635);
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
    setState(637);
    match(LangParser::FOR);
    setState(638);
    match(LangParser::OP);
    setState(639);
    forControl();
    setState(640);
    match(LangParser::CP);
    setState(641);
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

//----------------- ForEachControlContext ------------------------------------------------------------------

std::vector<LangParser::ForEachVarContext *> LangParser::ForEachControlContext::forEachVar() {
  return getRuleContexts<LangParser::ForEachVarContext>();
}

LangParser::ForEachVarContext* LangParser::ForEachControlContext::forEachVar(size_t i) {
  return getRuleContext<LangParser::ForEachVarContext>(i);
}

tree::TerminalNode* LangParser::ForEachControlContext::COL() {
  return getToken(LangParser::COL, 0);
}

LangParser::ExpressionListContext* LangParser::ForEachControlContext::expressionList() {
  return getRuleContext<LangParser::ExpressionListContext>(0);
}

std::vector<tree::TerminalNode *> LangParser::ForEachControlContext::COMMA() {
  return getTokens(LangParser::COMMA);
}

tree::TerminalNode* LangParser::ForEachControlContext::COMMA(size_t i) {
  return getToken(LangParser::COMMA, i);
}

LangParser::ForEachControlContext::ForEachControlContext(ForControlContext *ctx) { copyFrom(ctx); }


std::any LangParser::ForEachControlContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForEachControl(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ForNumericControlContext ------------------------------------------------------------------

LangParser::ForNumericVarContext* LangParser::ForNumericControlContext::forNumericVar() {
  return getRuleContext<LangParser::ForNumericVarContext>(0);
}

tree::TerminalNode* LangParser::ForNumericControlContext::ASSIGN() {
  return getToken(LangParser::ASSIGN, 0);
}

std::vector<LangParser::ExpressionContext *> LangParser::ForNumericControlContext::expression() {
  return getRuleContexts<LangParser::ExpressionContext>();
}

LangParser::ExpressionContext* LangParser::ForNumericControlContext::expression(size_t i) {
  return getRuleContext<LangParser::ExpressionContext>(i);
}

std::vector<tree::TerminalNode *> LangParser::ForNumericControlContext::COMMA() {
  return getTokens(LangParser::COMMA);
}

tree::TerminalNode* LangParser::ForNumericControlContext::COMMA(size_t i) {
  return getToken(LangParser::COMMA, i);
}

LangParser::ForNumericControlContext::ForNumericControlContext(ForControlContext *ctx) { copyFrom(ctx); }


std::any LangParser::ForNumericControlContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForNumericControl(this);
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
    setState(663);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::ForNumericControlContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(643);
      forNumericVar();
      setState(644);
      match(LangParser::ASSIGN);
      setState(645);
      expression();
      setState(646);
      match(LangParser::COMMA);
      setState(647);
      expression();
      setState(650);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == LangParser::COMMA) {
        setState(648);
        match(LangParser::COMMA);
        setState(649);
        expression();
      }
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<LangParser::ForEachControlContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(652);
      forEachVar();
      setState(657);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == LangParser::COMMA) {
        setState(653);
        match(LangParser::COMMA);
        setState(654);
        forEachVar();
        setState(659);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(660);
      match(LangParser::COL);
      setState(661);
      expressionList();
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

//----------------- ForNumericVarContext ------------------------------------------------------------------

LangParser::ForNumericVarContext::ForNumericVarContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::ForNumericVarContext::getRuleIndex() const {
  return LangParser::RuleForNumericVar;
}

void LangParser::ForNumericVarContext::copyFrom(ForNumericVarContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ForNumericVarTypedContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::ForNumericVarTypedContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

LangParser::TypeContext* LangParser::ForNumericVarTypedContext::type() {
  return getRuleContext<LangParser::TypeContext>(0);
}

tree::TerminalNode* LangParser::ForNumericVarTypedContext::AUTO() {
  return getToken(LangParser::AUTO, 0);
}

LangParser::ForNumericVarTypedContext::ForNumericVarTypedContext(ForNumericVarContext *ctx) { copyFrom(ctx); }


std::any LangParser::ForNumericVarTypedContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForNumericVarTyped(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ForNumericVarUntypedContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::ForNumericVarUntypedContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

LangParser::ForNumericVarUntypedContext::ForNumericVarUntypedContext(ForNumericVarContext *ctx) { copyFrom(ctx); }


std::any LangParser::ForNumericVarUntypedContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForNumericVarUntyped(this);
  else
    return visitor->visitChildren(this);
}
LangParser::ForNumericVarContext* LangParser::forNumericVar() {
  ForNumericVarContext *_localctx = _tracker.createInstance<ForNumericVarContext>(_ctx, getState());
  enterRule(_localctx, 106, LangParser::RuleForNumericVar);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(671);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::ForNumericVarTypedContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(667);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case LangParser::INT:
        case LangParser::FLOAT:
        case LangParser::NUMBER:
        case LangParser::STR:
        case LangParser::BOOL:
        case LangParser::ANY:
        case LangParser::VOID:
        case LangParser::NULL_:
        case LangParser::LIST:
        case LangParser::MAP:
        case LangParser::FUNCTION:
        case LangParser::COROUTINE:
        case LangParser::IDENTIFIER: {
          setState(665);
          type();
          break;
        }

        case LangParser::AUTO: {
          setState(666);
          match(LangParser::AUTO);
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(669);
      match(LangParser::IDENTIFIER);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<LangParser::ForNumericVarUntypedContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(670);
      match(LangParser::IDENTIFIER);
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

//----------------- ForEachVarContext ------------------------------------------------------------------

LangParser::ForEachVarContext::ForEachVarContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t LangParser::ForEachVarContext::getRuleIndex() const {
  return LangParser::RuleForEachVar;
}

void LangParser::ForEachVarContext::copyFrom(ForEachVarContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ForEachVarTypedContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::ForEachVarTypedContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

LangParser::TypeContext* LangParser::ForEachVarTypedContext::type() {
  return getRuleContext<LangParser::TypeContext>(0);
}

tree::TerminalNode* LangParser::ForEachVarTypedContext::AUTO() {
  return getToken(LangParser::AUTO, 0);
}

LangParser::ForEachVarTypedContext::ForEachVarTypedContext(ForEachVarContext *ctx) { copyFrom(ctx); }


std::any LangParser::ForEachVarTypedContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForEachVarTyped(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ForEachVarUntypedContext ------------------------------------------------------------------

tree::TerminalNode* LangParser::ForEachVarUntypedContext::IDENTIFIER() {
  return getToken(LangParser::IDENTIFIER, 0);
}

LangParser::ForEachVarUntypedContext::ForEachVarUntypedContext(ForEachVarContext *ctx) { copyFrom(ctx); }


std::any LangParser::ForEachVarUntypedContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<LangParserVisitor*>(visitor))
    return parserVisitor->visitForEachVarUntyped(this);
  else
    return visitor->visitChildren(this);
}
LangParser::ForEachVarContext* LangParser::forEachVar() {
  ForEachVarContext *_localctx = _tracker.createInstance<ForEachVarContext>(_ctx, getState());
  enterRule(_localctx, 108, LangParser::RuleForEachVar);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(679);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 78, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<LangParser::ForEachVarTypedContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(675);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case LangParser::INT:
        case LangParser::FLOAT:
        case LangParser::NUMBER:
        case LangParser::STR:
        case LangParser::BOOL:
        case LangParser::ANY:
        case LangParser::VOID:
        case LangParser::NULL_:
        case LangParser::LIST:
        case LangParser::MAP:
        case LangParser::FUNCTION:
        case LangParser::COROUTINE:
        case LangParser::IDENTIFIER: {
          setState(673);
          type();
          break;
        }

        case LangParser::AUTO: {
          setState(674);
          match(LangParser::AUTO);
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(677);
      match(LangParser::IDENTIFIER);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<LangParser::ForEachVarUntypedContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(678);
      match(LangParser::IDENTIFIER);
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
  enterRule(_localctx, 110, LangParser::RuleParameterList);
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
    setState(694);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case LangParser::INT:
      case LangParser::FLOAT:
      case LangParser::NUMBER:
      case LangParser::STR:
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
        setState(681);
        parameter();
        setState(686);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 79, _ctx);
        while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
          if (alt == 1) {
            setState(682);
            match(LangParser::COMMA);
            setState(683);
            parameter(); 
          }
          setState(688);
          _errHandler->sync(this);
          alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 79, _ctx);
        }
        setState(691);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == LangParser::COMMA) {
          setState(689);
          match(LangParser::COMMA);
          setState(690);
          match(LangParser::DDD);
        }
        break;
      }

      case LangParser::DDD: {
        enterOuterAlt(_localctx, 2);
        setState(693);
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
  enterRule(_localctx, 112, LangParser::RuleParameter);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(696);
    type();
    setState(697);
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
  enterRule(_localctx, 114, LangParser::RuleArguments);
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
    setState(700);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 4971974022989351168) != 0) || ((((_la - 65) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 65)) & 31765) != 0)) {
      setState(699);
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
