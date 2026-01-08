#include "squirrel.h"
#include "compiler/compiler.h"
#include "compiler/ast.h"
#include <sstream>
#include <string>
#include "utils.h"


using namespace SQCompilation;


// Diagnostic collector for analyzeCode
static std::ostringstream* diagOutput = nullptr;
static bool diagFirst = true;

static void diagnosticHandler(HSQUIRRELVM /*v*/, const SQCompilerMessage* msg) {
    if (!diagOutput) return;

    if (!diagFirst) *diagOutput << ",";
    diagFirst = false;

    *diagOutput << "{"
        << "\"line\":" << msg->line
        << ",\"col\":" << msg->column
        << ",\"len\":" << msg->columnsWidth
        << ",\"file\":\"" << escapeJson(msg->fileName) << "\""
        << ",\"intId\":" << msg->intId
        << ",\"textId\":\"" << escapeJson(msg->textId) << "\""
        << ",\"message\":\"" << escapeJson(msg->message) << "\""
        << ",\"isError\":" << (msg->isError ? "true" : "false")
        << "}";
}

std::string analyzeCode(const std::string& source) {
    HSQUIRRELVM vm = sq_open(256);
    if (!vm) {
        return "{\"messages\":[]}";
    }

    // Set up diagnostic collection
    std::ostringstream out;
    diagOutput = &out;
    diagFirst = true;
    out << "{\"messages\":[";

    sq_resetanalyzerconfig();
    // TODO: Also search for local configs

    // All diagnostics (parse errors + static analysis) come through this callback
    sq_setcompilerdiaghandler(vm, diagnosticHandler);

    SqASTData* astData = sq_parsetoast(vm, source.c_str(), source.length(),
                                        "document", SQFalse, SQFalse);

    if (astData && astData->root) {
        sq_analyzeast(vm, astData, nullptr, source.c_str(), source.length());
        sq_releaseASTData(vm, astData);
    }

    out << "]}";

    diagOutput = nullptr;
    sq_close(vm);

    return out.str();
}
