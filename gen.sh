rm ./src/front/* -rf
antlr4 -Dlanguage=Cpp -visitor -no-listener LangLexer.g4 LangParser.g4 -o src/front
