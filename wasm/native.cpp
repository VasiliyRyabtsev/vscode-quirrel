#include <string>
#include <emscripten/bind.h>


std::string parseAndExtractSymbols(const std::string& source);
std::string analyzeCode(const std::string& source);
std::string findDeclarationAt(const std::string& source, int line, int col);
std::string extractSemanticTokens(const std::string& source);


EMSCRIPTEN_BINDINGS(quirrel_vscode) {
    emscripten::function("parseAndExtractSymbols", &parseAndExtractSymbols);
    emscripten::function("analyzeCode", &analyzeCode);
    emscripten::function("findDeclarationAt", &findDeclarationAt);
    emscripten::function("extractSemanticTokens", &extractSemanticTokens);
}
