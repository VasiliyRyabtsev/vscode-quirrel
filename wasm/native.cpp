#include "squirrel.h"
#include "compiler/compiler.h"
#include "compiler/ast.h"
#include <emscripten/bind.h>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

using namespace SQCompilation;

static std::string escapeJson(const char* s) {
    if (!s) return "";

    // Count length and special chars to reserve exact size
    size_t len = 0, extra = 0;
    for (const char* p = s; *p; ++p, ++len) {
        if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r' || *p == '\t')
            ++extra;
    }

    std::string result;
    result.reserve(len + extra);

    for (const char* p = s; *p; ++p) {
        switch (*p) {
            case '"':  result.append("\\\"", 2); break;
            case '\\': result.append("\\\\", 2); break;
            case '\n': result.append("\\n", 2); break;
            case '\r': result.append("\\r", 2); break;
            case '\t': result.append("\\t", 2); break;
            default:   result.push_back(*p); break;
        }
    }
    return result;
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

// Declaration finder for Go To Declaration feature
// Traverses AST with scope tracking to find declarations
class DeclarationFinder : public Visitor {
    // Target position (1-based line, 0-based column)
    int targetLine, targetCol;

    // Result
    bool found;
    Node* declarationNode;
    const char* declarationKind;

    // Scope tracking - simple linked list of symbol maps
    struct Symbol {
        const char* name;
        Node* node;
        const char* kind;
    };

    struct Scope {
        std::vector<Symbol> symbols;
        Scope* parent;

        Scope(Scope* p = nullptr) : parent(p) {}
    };

    Scope* currentScope;
    std::vector<Scope*> allScopes;  // For cleanup

    // Check if position is within node bounds
    bool containsPosition(Node* node) {
        int ls = node->lineStart();
        int le = node->lineEnd();
        int cs = node->columnStart();
        int ce = node->columnEnd();

        if (targetLine < ls || targetLine > le) return false;
        if (targetLine == ls && targetCol < cs) return false;
        if (targetLine == le && targetCol >= ce) return false;
        return true;
    }

    // Add a symbol to current scope
    void declareSymbol(const char* name, Node* node, const char* kind) {
        if (!name || !currentScope) return;
        currentScope->symbols.push_back({name, node, kind});
    }

    // Find declaration in scope chain (innermost first)
    Symbol* findSymbol(const char* name) {
        for (Scope* s = currentScope; s; s = s->parent) {
            // Search in reverse order for shadowing
            for (int i = (int)s->symbols.size() - 1; i >= 0; --i) {
                if (strcmp(s->symbols[i].name, name) == 0) {
                    return &s->symbols[i];
                }
            }
        }
        return nullptr;
    }

    // Push new scope
    void pushScope() {
        Scope* newScope = new Scope(currentScope);
        allScopes.push_back(newScope);
        currentScope = newScope;
    }

    // Pop scope
    void popScope() {
        if (currentScope && currentScope->parent) {
            currentScope = currentScope->parent;
        }
    }

    // Get name from class key expression
    const char* getClassName(ClassDecl* cls) {
        if (cls->classKey() && cls->classKey()->op() == TO_ID) {
            return static_cast<Id*>(cls->classKey())->name();
        }
        return nullptr;
    }

public:
    DeclarationFinder(int line, int col)
        : targetLine(line), targetCol(col)
        , found(false), declarationNode(nullptr), declarationKind(nullptr)
        , currentScope(nullptr) {
        pushScope();  // Root scope
    }

    ~DeclarationFinder() {
        for (Scope* s : allScopes) {
            delete s;
        }
    }

    bool isFound() const { return found; }
    Node* getDeclarationNode() const { return declarationNode; }
    const char* getDeclarationKind() const { return declarationKind; }

    // Main visitor entry
    void visitNode(Node* node) override {
        if (found) return;  // Already found, stop

        TreeOp op = node->op();

        switch (op) {
            case TO_BLOCK: {
                Block* block = static_cast<Block*>(node);
                // Only create new scope for non-root blocks
                bool needsScope = !block->isRoot();
                if (needsScope) pushScope();

                for (Statement* stmt : block->statements()) {
                    if (found) break;
                    stmt->visit(this);
                }

                if (needsScope) popScope();
                break;
            }

            case TO_FUNCTION:
            case TO_CONSTRUCTOR: {
                FunctionDecl* fn = static_cast<FunctionDecl*>(node);
                const char* name = fn->name();

                // Declare function in current scope (before entering its scope)
                if (name && *name) {
                    declareSymbol(name, fn, "function");
                }

                // Create scope for function body
                pushScope();

                // Add parameters
                for (ParamDecl* param : fn->parameters()) {
                    declareSymbol(param->name(), param, "parameter");
                }

                // Visit body
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
                    declareSymbol(name, cls, "class");
                }

                // Visit class base if present (might reference identifiers)
                if (cls->classBase()) {
                    cls->classBase()->visit(this);
                }

                // Visit members (methods might have bodies with identifiers)
                for (const auto& member : cls->members()) {
                    if (found) break;
                    if (member.value) {
                        member.value->visit(this);
                    }
                }
                break;
            }

            case TO_ENUM: {
                EnumDecl* enm = static_cast<EnumDecl*>(node);
                declareSymbol(enm->name(), enm, "enum");
                // Enum members are accessed as EnumName.Member, not as bare identifiers
                break;
            }

            case TO_VAR: {
                VarDecl* var = static_cast<VarDecl*>(node);
                const char* kind = var->isAssignable() ? "variable" : "binding";

                // Visit initializer first (before declaring, for cases like `let x = x + 1`)
                if (var->initializer()) {
                    var->initializer()->visit(this);
                }

                // Then declare
                declareSymbol(var->name(), var, kind);
                break;
            }

            case TO_CONST: {
                ConstDecl* con = static_cast<ConstDecl*>(node);

                // Visit value first
                if (con->value()) {
                    con->value()->visit(this);
                }

                declareSymbol(con->name(), con, "constant");
                break;
            }

            case TO_DECL_GROUP: {
                DeclGroup* dgrp = static_cast<DeclGroup*>(node);
                for (auto& decl : dgrp->declarations()) {
                    if (found) break;
                    decl->visit(this);
                }
                break;
            }

            case TO_DESTRUCTURE: {
                DestructuringDecl* destruct = static_cast<DestructuringDecl*>(node);

                // Visit initializer first
                if (destruct->initExpression()) {
                    destruct->initExpression()->visit(this);
                }

                // Then declare all bindings
                for (VarDecl* decl : destruct->declarations()) {
                    const char* kind = decl->isAssignable() ? "variable" : "binding";
                    declareSymbol(decl->name(), decl, kind);
                }
                break;
            }

            case TO_IMPORT: {
                ImportStmt* import = static_cast<ImportStmt*>(node);

                if (import->slots.empty()) {
                    // Whole module: import "module" or import "module" as alias
                    const char* name = import->moduleAlias ? import->moduleAlias : nullptr;
                    if (name) {
                        declareSymbol(name, import, "import");
                    }
                } else {
                    // Selective: from "module" import a, b, c
                    for (const SQModuleImportSlot& slot : import->slots) {
                        if (strcmp(slot.name, "*") == 0) continue;  // Skip wildcard
                        const char* name = slot.alias ? slot.alias : slot.name;
                        declareSymbol(name, import, "import");
                    }
                }
                break;
            }

            case TO_FOREACH: {
                ForeachStatement* loop = static_cast<ForeachStatement*>(node);

                // Visit container first (outside loop scope)
                if (loop->container()) {
                    loop->container()->visit(this);
                }

                // Create scope for loop variables
                pushScope();

                if (loop->idx()) {
                    declareSymbol(loop->idx()->name(), loop->idx(), "variable");
                }
                if (loop->val()) {
                    declareSymbol(loop->val()->name(), loop->val(), "variable");
                }

                if (loop->body()) {
                    loop->body()->visit(this);
                }

                popScope();
                break;
            }

            case TO_FOR: {
                ForStatement* loop = static_cast<ForStatement*>(node);

                // Create scope for loop (for initializer variables)
                pushScope();

                if (loop->initializer()) {
                    loop->initializer()->visit(this);
                }
                if (loop->condition()) {
                    loop->condition()->visit(this);
                }
                if (loop->modifier()) {
                    loop->modifier()->visit(this);
                }
                if (loop->body()) {
                    loop->body()->visit(this);
                }

                popScope();
                break;
            }

            case TO_WHILE: {
                WhileStatement* loop = static_cast<WhileStatement*>(node);
                if (loop->condition()) {
                    loop->condition()->visit(this);
                }
                if (loop->body()) {
                    loop->body()->visit(this);
                }
                break;
            }

            case TO_DOWHILE: {
                DoWhileStatement* loop = static_cast<DoWhileStatement*>(node);
                if (loop->body()) {
                    loop->body()->visit(this);
                }
                if (loop->condition()) {
                    loop->condition()->visit(this);
                }
                break;
            }

            case TO_TRY: {
                TryStatement* tryStmt = static_cast<TryStatement*>(node);

                // Visit try block
                if (tryStmt->tryStatement()) {
                    tryStmt->tryStatement()->visit(this);
                }

                // Create scope for catch block
                pushScope();

                if (tryStmt->exceptionId()) {
                    declareSymbol(tryStmt->exceptionId()->name(), tryStmt->exceptionId(), "exception");
                }

                if (tryStmt->catchStatement()) {
                    tryStmt->catchStatement()->visit(this);
                }

                popScope();
                break;
            }

            case TO_IF: {
                IfStatement* ifStmt = static_cast<IfStatement*>(node);
                if (ifStmt->condition()) {
                    ifStmt->condition()->visit(this);
                }
                if (ifStmt->thenBranch()) {
                    ifStmt->thenBranch()->visit(this);
                }
                if (ifStmt->elseBranch()) {
                    ifStmt->elseBranch()->visit(this);
                }
                break;
            }

            case TO_SWITCH: {
                SwitchStatement* sw = static_cast<SwitchStatement*>(node);
                if (sw->expression()) {
                    sw->expression()->visit(this);
                }
                for (const auto& c : sw->cases()) {
                    if (found) break;
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
                if (term->argument()) {
                    term->argument()->visit(this);
                }
                break;
            }

            case TO_EXPR_STMT: {
                ExprStatement* estmt = static_cast<ExprStatement*>(node);
                if (estmt->expression()) {
                    estmt->expression()->visit(this);
                }
                break;
            }

            case TO_ID: {
                Id* id = static_cast<Id*>(node);
                // Check if this is the target identifier
                if (containsPosition(id)) {
                    Symbol* sym = findSymbol(id->name());
                    if (sym) {
                        found = true;
                        declarationNode = sym->node;
                        declarationKind = sym->kind;
                    }
                }
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
                if (call->callee()) {
                    call->callee()->visit(this);
                }
                for (Expr* arg : call->arguments()) {
                    if (found) break;
                    arg->visit(this);
                }
                break;
            }

            case TO_GETFIELD: {
                GetFieldExpr* gf = static_cast<GetFieldExpr*>(node);
                // Only visit receiver, not the field name (field is a member access)
                if (gf->receiver()) {
                    gf->receiver()->visit(this);
                }
                break;
            }

            case TO_SETFIELD: {
                SetFieldExpr* sf = static_cast<SetFieldExpr*>(node);
                if (sf->receiver()) {
                    sf->receiver()->visit(this);
                }
                if (sf->value()) {
                    sf->value()->visit(this);
                }
                break;
            }

            case TO_GETSLOT: {
                GetSlotExpr* gs = static_cast<GetSlotExpr*>(node);
                if (gs->receiver()) {
                    gs->receiver()->visit(this);
                }
                if (gs->key()) {
                    gs->key()->visit(this);
                }
                break;
            }

            case TO_SETSLOT: {
                SetSlotExpr* ss = static_cast<SetSlotExpr*>(node);
                if (ss->receiver()) {
                    ss->receiver()->visit(this);
                }
                if (ss->key()) {
                    ss->key()->visit(this);
                }
                if (ss->value()) {
                    ss->value()->visit(this);
                }
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
                    if (found) break;
                    e->visit(this);
                }
                break;
            }

            case TO_COMMA: {
                CommaExpr* comma = static_cast<CommaExpr*>(node);
                for (Expr* e : comma->expressions()) {
                    if (found) break;
                    e->visit(this);
                }
                break;
            }

            case TO_TABLE: {
                TableDecl* tbl = static_cast<TableDecl*>(node);
                for (const auto& member : tbl->members()) {
                    if (found) break;
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
                    // Binary expression
                    BinExpr* bin = static_cast<BinExpr*>(node);
                    if (bin->lhs()) bin->lhs()->visit(this);
                    if (bin->rhs()) bin->rhs()->visit(this);
                } else if ((TO_NOT <= op && op <= TO_CLONE) || op == TO_PAREN ||
                           op == TO_DELETE || op == TO_STATIC_MEMO || op == TO_INLINE_CONST) {
                    // Unary expression
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

std::string findDeclarationAt(const std::string& source, int line, int col) {
    HSQUIRRELVM vm = sq_open(256);
    if (!vm) {
        return "{\"found\":false}";
    }

    SqASTData* astData = sq_parsetoast(vm, source.c_str(), source.length(),
                                        "document", SQFalse, SQFalse);

    if (!astData || !astData->root) {
        sq_close(vm);
        return "{\"found\":false}";
    }

    DeclarationFinder finder(line, col);
    astData->root->visit(&finder);

    std::ostringstream out;
    if (finder.isFound() && finder.getDeclarationNode()) {
        Node* decl = finder.getDeclarationNode();
        out << "{\"found\":true,\"location\":{"
            << "\"line\":" << decl->lineStart()
            << ",\"col\":" << decl->columnStart()
            << ",\"endLine\":" << decl->lineEnd()
            << ",\"endCol\":" << decl->columnEnd()
            << ",\"kind\":\"" << finder.getDeclarationKind() << "\"}}";
    } else {
        out << "{\"found\":false}";
    }

    sq_releaseASTData(vm, astData);
    sq_close(vm);
    return out.str();
}

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

EMSCRIPTEN_BINDINGS(quirrel_vscode) {
    emscripten::function("parseAndExtractSymbols", &parseAndExtractSymbols);
    emscripten::function("analyzeCode", &analyzeCode);
    emscripten::function("findDeclarationAt", &findDeclarationAt);
}
