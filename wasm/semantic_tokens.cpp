#include "squirrel.h"
#include "compiler/compiler.h"
#include "compiler/ast.h"
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include "utils.h"


using namespace SQCompilation;


// Semantic token extractor for VS Code semantic highlighting
// Traverses entire AST with scope tracking to classify all identifiers
class SemanticTokenExtractor : public Visitor {
    // Token types (must match TypeScript legend order)
    enum TokenType {
        TT_VARIABLE = 0,
        TT_PARAMETER = 1,
        TT_FUNCTION = 2,
        TT_CLASS = 3,
        TT_ENUM = 4,
        TT_ENUM_MEMBER = 5,
        TT_PROPERTY = 6,
        TT_IMPORT = 7
    };

    // Token modifiers (bitmask, must match TypeScript legend order)
    enum TokenModifier {
        TM_DECLARATION = 1 << 0,
        TM_READONLY = 1 << 1
    };

    struct SemanticToken {
        int line;      // 1-based
        int col;       // 0-based
        int length;
        int type;
        int modifiers;
    };

    std::vector<SemanticToken> tokens;

    // Source text and line index for finding name positions
    const std::string& source;
    std::vector<size_t> lineOffsets;  // Offset of each line start in source

    // Scope tracking (same structure as DeclarationFinder)
    struct Symbol {
        const char* name;
        Node* node;
        const char* kind;
        bool isReadonly;
    };

    struct Scope {
        std::vector<Symbol> symbols;
        Scope* parent;

        Scope(Scope* p = nullptr) : parent(p) {}
    };

    Scope* currentScope;
    std::vector<Scope*> allScopes;

    void buildLineIndex() {
        lineOffsets.push_back(0);  // Line 1 starts at offset 0
        for (size_t i = 0; i < source.size(); ++i) {
            if (source[i] == '\n') {
                lineOffsets.push_back(i + 1);
            }
        }
    }

    // Find name position within a line, starting from startCol
    // Returns the column where name starts, or -1 if not found
    int findNameInLine(int line, int startCol, const char* name) {
        if (line < 1 || line > (int)lineOffsets.size()) return -1;

        size_t lineStart = lineOffsets[line - 1];
        size_t lineEnd = (line < (int)lineOffsets.size()) ? lineOffsets[line] : source.size();

        size_t searchStart = lineStart + startCol;
        if (searchStart >= lineEnd) return -1;

        size_t nameLen = strlen(name);
        const char* linePtr = source.c_str() + searchStart;
        size_t searchLen = lineEnd - searchStart;

        // Search for the name as a whole word
        for (size_t i = 0; i + nameLen <= searchLen; ++i) {
            if (strncmp(linePtr + i, name, nameLen) == 0) {
                // Check it's a whole word (not part of a larger identifier)
                bool startOk = (i == 0) || !isalnum(linePtr[i - 1]) && linePtr[i - 1] != '_';
                bool endOk = (i + nameLen >= searchLen) || (!isalnum(linePtr[i + nameLen]) && linePtr[i + nameLen] != '_');
                if (startOk && endOk) {
                    return startCol + (int)i;
                }
            }
        }
        return -1;
    }

    void declareSymbol(const char* name, Node* node, const char* kind, bool isReadonly = false) {
        if (!name || !currentScope) return;
        currentScope->symbols.push_back({name, node, kind, isReadonly});
    }

    Symbol* findSymbol(const char* name) {
        for (Scope* s = currentScope; s; s = s->parent) {
            for (int i = (int)s->symbols.size() - 1; i >= 0; --i) {
                if (strcmp(s->symbols[i].name, name) == 0) {
                    return &s->symbols[i];
                }
            }
        }
        return nullptr;
    }

    void pushScope() {
        Scope* newScope = new Scope(currentScope);
        allScopes.push_back(newScope);
        currentScope = newScope;
    }

    void popScope() {
        if (currentScope && currentScope->parent) {
            currentScope = currentScope->parent;
        }
    }

    void addToken(int line, int col, int length, TokenType type, int modifiers) {
        if (length > 0 && col >= 0) {
            tokens.push_back({line, col, length, type, modifiers});
        }
    }

    void addTokenForNode(Node* node, int length, TokenType type, int modifiers) {
        addToken(node->lineStart(), node->columnStart(), length, type, modifiers);
    }

    // Add token for a named declaration, searching for the name in source
    void addTokenForNamedDecl(Node* node, const char* name, TokenType type, int modifiers) {
        if (!name || !*name) return;
        int len = (int)strlen(name);
        int line = node->lineStart();
        int col = findNameInLine(line, node->columnStart(), name);
        if (col >= 0) {
            addToken(line, col, len, type, modifiers);
        }
    }

    TokenType kindToTokenType(const char* kind) {
        if (strcmp(kind, "variable") == 0) return TT_VARIABLE;
        if (strcmp(kind, "binding") == 0) return TT_VARIABLE;
        if (strcmp(kind, "parameter") == 0) return TT_PARAMETER;
        if (strcmp(kind, "function") == 0) return TT_FUNCTION;
        if (strcmp(kind, "class") == 0) return TT_CLASS;
        if (strcmp(kind, "enum") == 0) return TT_ENUM;
        if (strcmp(kind, "constant") == 0) return TT_VARIABLE;
        if (strcmp(kind, "import") == 0) return TT_IMPORT;
        if (strcmp(kind, "exception") == 0) return TT_VARIABLE;
        return TT_VARIABLE;
    }

    const char* getClassName(ClassDecl* cls) {
        if (cls->classKey() && cls->classKey()->op() == TO_ID) {
            return static_cast<Id*>(cls->classKey())->name();
        }
        return nullptr;
    }

public:
    SemanticTokenExtractor(const std::string& src) : source(src), currentScope(nullptr) {
        buildLineIndex();
        pushScope();
    }

    ~SemanticTokenExtractor() {
        for (Scope* s : allScopes) {
            delete s;
        }
    }

    std::string toJson() {
        // Sort tokens by position (line, then column) - VS Code requires this
        std::sort(tokens.begin(), tokens.end(), [](const SemanticToken& a, const SemanticToken& b) {
            if (a.line != b.line) return a.line < b.line;
            return a.col < b.col;
        });

        std::ostringstream out;
        out << "{\"tokens\":[";
        bool first = true;
        for (const auto& tok : tokens) {
            if (!first) out << ",";
            first = false;
            out << "{\"line\":" << tok.line
                << ",\"col\":" << tok.col
                << ",\"length\":" << tok.length
                << ",\"type\":" << tok.type
                << ",\"modifiers\":" << tok.modifiers << "}";
        }
        out << "]}";
        return out.str();
    }

    void visitNode(Node* node) override {
        TreeOp op = node->op();

        switch (op) {
            case TO_BLOCK: {
                Block* block = static_cast<Block*>(node);
                bool needsScope = !block->isRoot();
                if (needsScope) pushScope();

                for (Statement* stmt : block->statements()) {
                    stmt->visit(this);
                }

                if (needsScope) popScope();
                break;
            }

            case TO_FUNCTION:
            case TO_CONSTRUCTOR: {
                FunctionDecl* fn = static_cast<FunctionDecl*>(node);
                const char* name = fn->name();

                // Emit token for function name declaration
                if (name && *name) {
                    addTokenForNamedDecl(fn, name, TT_FUNCTION, TM_DECLARATION);
                    declareSymbol(name, fn, "function", false);
                }

                pushScope();

                // Add parameters - ParamDecl position is at the param name
                for (ParamDecl* param : fn->parameters()) {
                    const char* pname = param->name();
                    if (pname && *pname) {
                        int len = (int)strlen(pname);
                        addTokenForNode(param, len, TT_PARAMETER, TM_DECLARATION);
                        declareSymbol(pname, param, "parameter", false);
                    }
                }

                if (fn->body()) {
                    fn->body()->visit(this);
                }

                popScope();
                break;
            }

            case TO_CLASS: {
                ClassDecl* cls = static_cast<ClassDecl*>(node);
                const char* name = getClassName(cls);

                if (name) {
                    // Emit token for class key (the identifier)
                    Id* classKey = static_cast<Id*>(cls->classKey());
                    int len = (int)strlen(name);
                    addTokenForNode(classKey, len, TT_CLASS, TM_DECLARATION);
                    declareSymbol(name, cls, "class", false);
                }

                // Visit class base if present
                if (cls->classBase()) {
                    cls->classBase()->visit(this);
                }

                // Visit members
                for (const auto& member : cls->members()) {
                    if (member.value) {
                        member.value->visit(this);
                    }
                }
                break;
            }

            case TO_ENUM: {
                EnumDecl* enm = static_cast<EnumDecl*>(node);
                const char* name = enm->name();

                if (name && *name) {
                    addTokenForNamedDecl(enm, name, TT_ENUM, TM_DECLARATION);
                    declareSymbol(name, enm, "enum", false);
                }
                // Enum members are accessed as EnumName.Member, handled in GETFIELD
                break;
            }

            case TO_VAR: {
                VarDecl* var = static_cast<VarDecl*>(node);
                const char* name = var->name();
                bool readonly = !var->isAssignable();

                // Visit initializer first (before declaring)
                if (var->initializer()) {
                    var->initializer()->visit(this);
                }

                // Emit token for variable name
                if (name && *name) {
                    int mods = TM_DECLARATION | (readonly ? TM_READONLY : 0);
                    addTokenForNamedDecl(var, name, TT_VARIABLE, mods);
                    declareSymbol(name, var, readonly ? "binding" : "variable", readonly);
                }
                break;
            }

            case TO_CONST: {
                ConstDecl* con = static_cast<ConstDecl*>(node);
                const char* name = con->name();

                if (con->value()) {
                    con->value()->visit(this);
                }

                if (name && *name) {
                    addTokenForNamedDecl(con, name, TT_VARIABLE, TM_DECLARATION | TM_READONLY);
                    declareSymbol(name, con, "constant", true);
                }
                break;
            }

            case TO_DECL_GROUP: {
                DeclGroup* dgrp = static_cast<DeclGroup*>(node);
                for (auto& decl : dgrp->declarations()) {
                    decl->visit(this);
                }
                break;
            }

            case TO_DESTRUCTURE: {
                DestructuringDecl* destruct = static_cast<DestructuringDecl*>(node);

                if (destruct->initExpression()) {
                    destruct->initExpression()->visit(this);
                }

                for (VarDecl* decl : destruct->declarations()) {
                    const char* name = decl->name();
                    bool readonly = !decl->isAssignable();
                    if (name && *name) {
                        int mods = TM_DECLARATION | (readonly ? TM_READONLY : 0);
                        addTokenForNamedDecl(decl, name, TT_VARIABLE, mods);
                        declareSymbol(name, decl, readonly ? "binding" : "variable", readonly);
                    }
                }
                break;
            }

            case TO_IMPORT: {
                ImportStmt* import = static_cast<ImportStmt*>(node);

                if (import->slots.empty()) {
                    // import "module" as alias
                    const char* name = import->moduleAlias ? import->moduleAlias : nullptr;
                    if (name) {
                        // Search for the alias after "as" keyword to avoid matching in module path
                        int line = import->lineStart();
                        int len = (int)strlen(name);
                        // Find " as " first, then search for alias after it
                        int asCol = findNameInLine(line, import->columnStart(), "as");
                        if (asCol >= 0) {
                            int aliasCol = findNameInLine(line, asCol + 2, name);
                            if (aliasCol >= 0) {
                                addToken(line, aliasCol, len, TT_IMPORT, TM_DECLARATION);
                            }
                        }
                        declareSymbol(name, import, "import", true);
                    }
                } else {
                    // from "module" import name, name as alias, ...
                    for (const SQModuleImportSlot& slot : import->slots) {
                        if (strcmp(slot.name, "*") == 0) continue;

                        // Use slot's line/column directly - it points to the imported name
                        int len;
                        int line = slot.line;
                        int col = slot.column;

                        if (slot.alias) {
                            // If there's an alias, we need to find its position
                            // The alias comes after "name as alias", so search after slot position
                            const char* name = slot.alias;
                            len = (int)strlen(name);
                            // Search for alias after the original name position
                            int aliasCol = findNameInLine(line, col + (int)strlen(slot.name), name);
                            if (aliasCol >= 0) {
                                addToken(line, aliasCol, len, TT_IMPORT, TM_DECLARATION);
                            }
                            declareSymbol(name, import, "import", true);
                        } else {
                            // No alias - use slot position directly
                            len = (int)strlen(slot.name);
                            addToken(line, col, len, TT_IMPORT, TM_DECLARATION);
                            declareSymbol(slot.name, import, "import", true);
                        }
                    }
                }
                break;
            }

            case TO_FOREACH: {
                ForeachStatement* loop = static_cast<ForeachStatement*>(node);

                if (loop->container()) {
                    loop->container()->visit(this);
                }

                pushScope();

                if (loop->idx()) {
                    const char* name = loop->idx()->name();
                    if (name && *name) {
                        // foreach loop vars - search for name in source
                        addTokenForNamedDecl(loop, name, TT_VARIABLE, TM_DECLARATION);
                        declareSymbol(name, loop->idx(), "variable", false);
                    }
                }
                if (loop->val()) {
                    const char* name = loop->val()->name();
                    if (name && *name) {
                        addTokenForNamedDecl(loop, name, TT_VARIABLE, TM_DECLARATION);
                        declareSymbol(name, loop->val(), "variable", false);
                    }
                }

                if (loop->body()) {
                    loop->body()->visit(this);
                }

                popScope();
                break;
            }

            case TO_FOR: {
                ForStatement* loop = static_cast<ForStatement*>(node);

                pushScope();

                if (loop->initializer()) loop->initializer()->visit(this);
                if (loop->condition()) loop->condition()->visit(this);
                if (loop->modifier()) loop->modifier()->visit(this);
                if (loop->body()) loop->body()->visit(this);

                popScope();
                break;
            }

            case TO_WHILE: {
                WhileStatement* loop = static_cast<WhileStatement*>(node);
                if (loop->condition()) loop->condition()->visit(this);
                if (loop->body()) loop->body()->visit(this);
                break;
            }

            case TO_DOWHILE: {
                DoWhileStatement* loop = static_cast<DoWhileStatement*>(node);
                if (loop->body()) loop->body()->visit(this);
                if (loop->condition()) loop->condition()->visit(this);
                break;
            }

            case TO_TRY: {
                TryStatement* tryStmt = static_cast<TryStatement*>(node);

                if (tryStmt->tryStatement()) {
                    tryStmt->tryStatement()->visit(this);
                }

                pushScope();

                if (tryStmt->exceptionId()) {
                    const char* name = tryStmt->exceptionId()->name();
                    if (name && *name) {
                        // Exception var - the Id node should have correct position
                        int len = (int)strlen(name);
                        addTokenForNode(tryStmt->exceptionId(), len, TT_VARIABLE, TM_DECLARATION);
                        declareSymbol(name, tryStmt->exceptionId(), "exception", false);
                    }
                }

                if (tryStmt->catchStatement()) {
                    tryStmt->catchStatement()->visit(this);
                }

                popScope();
                break;
            }

            case TO_IF: {
                IfStatement* ifStmt = static_cast<IfStatement*>(node);
                if (ifStmt->condition()) ifStmt->condition()->visit(this);
                if (ifStmt->thenBranch()) ifStmt->thenBranch()->visit(this);
                if (ifStmt->elseBranch()) ifStmt->elseBranch()->visit(this);
                break;
            }

            case TO_SWITCH: {
                SwitchStatement* sw = static_cast<SwitchStatement*>(node);
                if (sw->expression()) sw->expression()->visit(this);
                for (const auto& c : sw->cases()) {
                    if (c.val) c.val->visit(this);
                    if (c.stmt) c.stmt->visit(this);
                }
                if (sw->defaultCase().stmt) {
                    sw->defaultCase().stmt->visit(this);
                }
                break;
            }

            case TO_RETURN:
            case TO_YIELD:
            case TO_THROW: {
                TerminateStatement* term = static_cast<TerminateStatement*>(node);
                if (term->argument()) term->argument()->visit(this);
                break;
            }

            case TO_EXPR_STMT: {
                ExprStatement* estmt = static_cast<ExprStatement*>(node);
                if (estmt->expression()) estmt->expression()->visit(this);
                break;
            }

            case TO_ID: {
                Id* id = static_cast<Id*>(node);
                const char* name = id->name();

                // Skip special identifiers
                if (!name || !*name) break;
                if (strcmp(name, "this") == 0 || strcmp(name, "base") == 0) break;

                Symbol* sym = findSymbol(name);
                if (sym) {
                    TokenType type = kindToTokenType(sym->kind);
                    int mods = sym->isReadonly ? TM_READONLY : 0;
                    int len = (int)strlen(name);
                    addTokenForNode(id, len, type, mods);
                }
                // Unknown identifiers (globals, builtins) - don't emit token
                break;
            }

            case TO_DECL_EXPR: {
                DeclExpr* declExpr = static_cast<DeclExpr*>(node);
                if (declExpr->declaration()) {
                    declExpr->declaration()->visit(this);
                }
                break;
            }

            case TO_CALL: {
                CallExpr* call = static_cast<CallExpr*>(node);
                if (call->callee()) call->callee()->visit(this);
                for (Expr* arg : call->arguments()) {
                    arg->visit(this);
                }
                break;
            }

            case TO_GETFIELD: {
                GetFieldExpr* gf = static_cast<GetFieldExpr*>(node);

                // Check if this is an enum member access (EnumName.MEMBER)
                if (gf->receiver() && gf->receiver()->op() == TO_ID) {
                    Id* receiver = static_cast<Id*>(gf->receiver());
                    Symbol* sym = findSymbol(receiver->name());
                    if (sym && strcmp(sym->kind, "enum") == 0) {
                        // Emit enum token for receiver
                        int len = (int)strlen(receiver->name());
                        addTokenForNode(receiver, len, TT_ENUM, 0);

                        // Emit enumMember token for field
                        const char* fieldName = gf->fieldName();
                        if (fieldName && *fieldName) {
                            // Calculate field position: end of expression minus field length
                            int fieldLen = (int)strlen(fieldName);
                            int fieldCol = gf->columnEnd() - fieldLen;
                            addToken(gf->lineEnd(), fieldCol, fieldLen, TT_ENUM_MEMBER, TM_READONLY);
                        }
                        break;
                    }
                }

                // Regular field access - visit receiver, emit property for field
                if (gf->receiver()) {
                    gf->receiver()->visit(this);
                }

                // Emit property token for field name
                const char* fieldName = gf->fieldName();
                if (fieldName && *fieldName) {
                    int fieldLen = (int)strlen(fieldName);
                    int fieldCol = gf->columnEnd() - fieldLen;
                    addToken(gf->lineEnd(), fieldCol, fieldLen, TT_PROPERTY, 0);
                }
                break;
            }

            case TO_SETFIELD: {
                SetFieldExpr* sf = static_cast<SetFieldExpr*>(node);
                if (sf->receiver()) sf->receiver()->visit(this);
                // Note: SetFieldExpr doesn't have fieldName() accessor, so we skip the field token
                if (sf->value()) sf->value()->visit(this);
                break;
            }

            case TO_GETSLOT: {
                GetSlotExpr* gs = static_cast<GetSlotExpr*>(node);
                if (gs->receiver()) gs->receiver()->visit(this);
                if (gs->key()) gs->key()->visit(this);
                break;
            }

            case TO_SETSLOT: {
                SetSlotExpr* ss = static_cast<SetSlotExpr*>(node);
                if (ss->receiver()) ss->receiver()->visit(this);
                if (ss->key()) ss->key()->visit(this);
                if (ss->value()) ss->value()->visit(this);
                break;
            }

            case TO_TERNARY: {
                TerExpr* ter = static_cast<TerExpr*>(node);
                if (ter->a()) ter->a()->visit(this);
                if (ter->b()) ter->b()->visit(this);
                if (ter->c()) ter->c()->visit(this);
                break;
            }

            case TO_ARRAYEXPR: {
                ArrayExpr* arr = static_cast<ArrayExpr*>(node);
                for (Expr* e : arr->initializers()) {
                    e->visit(this);
                }
                break;
            }

            case TO_COMMA: {
                CommaExpr* comma = static_cast<CommaExpr*>(node);
                for (Expr* e : comma->expressions()) {
                    e->visit(this);
                }
                break;
            }

            case TO_TABLE: {
                TableDecl* tbl = static_cast<TableDecl*>(node);
                for (const auto& member : tbl->members()) {
                    if (member.value) {
                        member.value->visit(this);
                    }
                }
                break;
            }

            case TO_CODE_BLOCK_EXPR: {
                CodeBlockExpr* cbe = static_cast<CodeBlockExpr*>(node);
                if (cbe->block()) {
                    cbe->block()->visit(this);
                }
                break;
            }

            // Binary and unary expressions
            default:
                if (TO_NULLC <= op && op <= TO_MODEQ) {
                    BinExpr* bin = static_cast<BinExpr*>(node);
                    if (bin->lhs()) bin->lhs()->visit(this);
                    if (bin->rhs()) bin->rhs()->visit(this);
                } else if ((TO_NOT <= op && op <= TO_CLONE) || op == TO_PAREN ||
                           op == TO_DELETE || op == TO_STATIC_MEMO || op == TO_INLINE_CONST) {
                    UnExpr* un = static_cast<UnExpr*>(node);
                    if (un->argument()) un->argument()->visit(this);
                } else if (op == TO_INC) {
                    IncExpr* inc = static_cast<IncExpr*>(node);
                    if (inc->argument()) inc->argument()->visit(this);
                }
                break;
        }
    }
};

std::string extractSemanticTokens(const std::string& source) {
    HSQUIRRELVM vm = sq_open(256);
    if (!vm) {
        return "{\"tokens\":[]}";
    }

    SqASTData* astData = sq_parsetoast(vm, source.c_str(), source.length(),
                                        "document", SQFalse, SQFalse);

    if (!astData || !astData->root) {
        sq_close(vm);
        return "{\"tokens\":[]}";
    }

    SemanticTokenExtractor extractor(source);
    astData->root->visit(&extractor);

    std::string result = extractor.toJson();

    sq_releaseASTData(vm, astData);
    sq_close(vm);
    return result;
}
