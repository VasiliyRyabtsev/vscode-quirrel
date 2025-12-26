#include "squirrel.h"
#include "compiler/compiler.h"
#include "compiler/ast.h"
#include <sstream>
#include <string>
#include "utils.h"


using namespace SQCompilation;


// Store last error message for parseAndExtractSymbols
static std::string lastError;


static void compileErrorHandler(HSQUIRRELVM /*v*/, SQMessageSeverity /*sev*/,
    const SQChar* desc, const SQChar* /*source*/,
    SQInteger line, SQInteger column, const SQChar* /*extra*/)
{
    std::ostringstream err;
    err << "Line " << line << ":" << column << ": " << desc;
    lastError = err.str();
}


// Symbol extractor visitor that outputs JSON
class SymbolExtractor : public Visitor {
    std::ostringstream& out;
    bool firstAtLevel[64];  // Track first element at each nesting level
    int depth;

    void startSymbol(const char* name, const char* kind, Node* node) {
        startSymbolWithRange(name, kind, node, node);
    }

    void startSymbolWithRange(const char* name, const char* kind, Node* startNode, Node* endNode) {
        if (!firstAtLevel[depth]) out << ",";
        firstAtLevel[depth] = false;

        out << "{\"name\":\"" << escapeJson(name) << "\""
            << ",\"kind\":\"" << kind << "\""
            << ",\"range\":{"
            << "\"startLine\":" << startNode->lineStart()
            << ",\"startCol\":" << startNode->columnStart()
            << ",\"endLine\":" << endNode->lineEnd()
            << ",\"endCol\":" << endNode->columnEnd()
            << "}";
    }

    void startChildren() {
        out << ",\"children\":[";
        depth++;
        if (depth < 64) firstAtLevel[depth] = true;
    }

    void endChildren() {
        out << "]";
        depth--;
    }

    void endSymbol() {
        out << "}";
    }

    // Get name from class key expression
    const char* getClassName(ClassDecl* cls) {
        if (cls->classKey() && cls->classKey()->op() == TO_ID) {
            return static_cast<Id*>(cls->classKey())->name();
        }
        return "<anonymous>";
    }

    // Get name from table member key
    const char* getMemberName(Expr* key) {
        if (key && key->op() == TO_ID) {
            return static_cast<Id*>(key)->name();
        }
        if (key && key->op() == TO_LITERAL) {
            LiteralExpr* lit = static_cast<LiteralExpr*>(key);
            if (lit->kind() == LK_STRING) {
                return lit->s();
            }
        }
        return nullptr;
    }

    // Visit a node and collect its children as a JSON string
    // Returns empty string if no children found
    std::string collectChildren(Node* node) {
        if (!node) return "";

        std::ostringstream childOut;
        std::swap(out, childOut);
        bool savedFirst = firstAtLevel[depth];
        firstAtLevel[depth] = true;

        node->visit(this);

        std::swap(out, childOut);
        firstAtLevel[depth] = savedFirst;

        return childOut.str();
    }

    // Visit a function/method body and emit children if any exist
    void visitFunctionBody(FunctionDecl* fn) {
        if (!fn->body()) return;

        std::string children = collectChildren(fn->body());
        if (!children.empty()) {
            out << ",\"children\":[" << children << "]";
        }
    }

    // Emit a table/class member (shared by class, table, const, and var handling)
    void emitTableMember(const TableMember& member) {
        const char* memberName = getMemberName(member.key);
        if (!memberName) return;

        Expr* val = member.value;
        Decl* memberDecl = nullptr;
        if (val->op() == TO_DECL_EXPR) {
            memberDecl = static_cast<DeclExpr*>(val)->declaration();
        }

        if ((memberDecl && memberDecl->op() == TO_FUNCTION) || val->op() == TO_FUNCTION) {
            FunctionDecl* method = memberDecl ? static_cast<FunctionDecl*>(memberDecl) : nullptr;
            startSymbol(memberName, "Method", member.value);
            if (method) visitFunctionBody(method);
            endSymbol();
        } else if ((memberDecl && memberDecl->op() == TO_CONSTRUCTOR) || val->op() == TO_CONSTRUCTOR) {
            FunctionDecl* ctor = memberDecl ? static_cast<FunctionDecl*>(memberDecl) : nullptr;
            startSymbol(memberName, "Constructor", member.value);
            if (ctor) visitFunctionBody(ctor);
            endSymbol();
        } else {
            startSymbol(memberName, member.isStatic() ? "Property" : "Field", member.key);
            endSymbol();
        }
    }

    // Emit children for a complex initializer (table or class)
    void emitInitializerChildren(Expr* init) {
        if (!init) return;

        Decl* decl = nullptr;
        if (init->op() == TO_DECL_EXPR) {
            decl = static_cast<DeclExpr*>(init)->declaration();
        }
        if (!decl) return;

        if (decl->op() == TO_TABLE) {
            TableDecl* tbl = static_cast<TableDecl*>(decl);
            bool hasMembers = false;
            for (const auto& member : tbl->members()) {
                if (!getMemberName(member.key)) continue;
                if (!hasMembers) {
                    startChildren();
                    hasMembers = true;
                }
                emitTableMember(member);
            }
            if (hasMembers) endChildren();
        } else if (decl->op() == TO_CLASS) {
            ClassDecl* cls = static_cast<ClassDecl*>(decl);
            bool hasMembers = false;
            for (const auto& member : cls->members()) {
                if (!getMemberName(member.key)) continue;
                if (!hasMembers) {
                    startChildren();
                    hasMembers = true;
                }
                emitTableMember(member);
            }
            if (hasMembers) endChildren();
        }
    }

    // Get end node for initializer (for accurate symbol range)
    Node* getInitializerEnd(Expr* init, Node* fallback) {
        if (!init) return fallback;
        if (init->op() == TO_DECL_EXPR) {
            return static_cast<DeclExpr*>(init)->declaration();
        }
        return init;
    }

public:
    SymbolExtractor(std::ostringstream& o) : out(o), depth(0) {
        for (int i = 0; i < 64; i++) firstAtLevel[i] = true;
    }

    virtual void visitNode(Node* node) override {
        TreeOp op = node->op();

        switch (op) {
            case TO_BLOCK: {
                Block* block = static_cast<Block*>(node);
                for (Statement* stmt : block->statements()) {
                    stmt->visit(this);
                }
                break;
            }

            case TO_FUNCTION:
            case TO_CONSTRUCTOR: {
                FunctionDecl* fn = static_cast<FunctionDecl*>(node);
                const char* name = fn->name();

                // Skip anonymous lambdas - they add noise to the outline
                if (!name || !*name) break;

                const char* kind = (op == TO_CONSTRUCTOR) ? "Constructor" : "Function";
                startSymbol(name, kind, fn);
                visitFunctionBody(fn);
                endSymbol();
                break;
            }

            case TO_CLASS: {
                ClassDecl* cls = static_cast<ClassDecl*>(node);
                startSymbol(getClassName(cls), "Class", cls);

                // Emit class members as children
                bool hasMembers = false;
                for (const auto& member : cls->members()) {
                    if (!getMemberName(member.key)) continue;
                    if (!hasMembers) {
                        startChildren();
                        hasMembers = true;
                    }
                    emitTableMember(member);
                }
                if (hasMembers) endChildren();

                endSymbol();
                break;
            }

            case TO_ENUM: {
                EnumDecl* enm = static_cast<EnumDecl*>(node);
                startSymbol(enm->name(), "Enum", enm);

                if (!enm->consts().empty()) {
                    startChildren();
                    for (const auto& c : enm->consts()) {
                        // Use enum node location since consts don't have their own location
                        startSymbol(c.id, "EnumMember", enm);
                        endSymbol();
                    }
                    endChildren();
                }

                endSymbol();
                break;
            }

            case TO_VAR: {
                VarDecl* var = static_cast<VarDecl*>(node);
                const char* kind = var->isAssignable() ? "Variable" : "Binding";
                Expr* init = var->initializer();

                startSymbolWithRange(var->name(), kind, var, getInitializerEnd(init, var));
                emitInitializerChildren(init);
                endSymbol();
                break;
            }

            case TO_CONST: {
                ConstDecl* con = static_cast<ConstDecl*>(node);
                Expr* init = con->value();

                startSymbolWithRange(con->name(), "Constant", con, getInitializerEnd(init, con));
                emitInitializerChildren(init);
                endSymbol();
                break;
            }

            case TO_DECL_GROUP: {
                DeclGroup* dgrp = static_cast<DeclGroup*>(node);
                for (auto& decl : dgrp->declarations()) {
                    decl->visit(this);
                }
                break;
            }

            case TO_TABLE: {
                // Only emit named tables (assigned to variables)
                // Anonymous table literals are not symbols
                break;
            }

            // Control flow - recurse into bodies without emitting symbols
            case TO_IF: {
                IfStatement* ifStmt = static_cast<IfStatement*>(node);
                if (ifStmt->thenBranch()) ifStmt->thenBranch()->visit(this);
                if (ifStmt->elseBranch()) ifStmt->elseBranch()->visit(this);
                break;
            }

            case TO_WHILE:
            case TO_FOR:
            case TO_FOREACH: {
                LoopStatement* loop = static_cast<LoopStatement*>(node);
                if (loop->body()) loop->body()->visit(this);
                break;
            }

            case TO_SWITCH: {
                SwitchStatement* sw = static_cast<SwitchStatement*>(node);
                for (const auto& c : sw->cases()) {
                    if (c.stmt) c.stmt->visit(this);
                }
                if (sw->defaultCase().stmt) sw->defaultCase().stmt->visit(this);
                break;
            }

            case TO_TRY: {
                TryStatement* tryStmt = static_cast<TryStatement*>(node);
                if (tryStmt->tryStatement()) tryStmt->tryStatement()->visit(this);
                if (tryStmt->catchStatement()) tryStmt->catchStatement()->visit(this);
                break;
            }

            default:
                // For other nodes (expressions, etc.), don't recurse
                break;
        }
    }
};


std::string parseAndExtractSymbols(const std::string& source) {
    lastError.clear();

    HSQUIRRELVM vm = sq_open(256);
    if (!vm) {
        return "{\"error\":\"Failed to create VM\",\"symbols\":[]}";
    }

    sq_setcompilererrorhandler(vm, compileErrorHandler);

    SqASTData* astData = sq_parsetoast(vm, source.c_str(), source.length(),
                                        "document", SQFalse, SQFalse);

    if (!astData || !astData->root) {
        std::string error = lastError.empty() ? "Parse failed" : lastError;
        sq_close(vm);

        std::ostringstream out;
        out << "{\"error\":\"" << escapeJson(error.c_str()) << "\",\"symbols\":[]}";
        return out.str();
    }

    std::ostringstream out;
    out << "{\"error\":null,\"symbols\":[";

    SymbolExtractor extractor(out);
    astData->root->visit(&extractor);

    out << "]}";

    sq_releaseASTData(vm, astData);
    sq_close(vm);

    return out.str();
}
