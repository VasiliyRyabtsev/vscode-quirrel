#include "squirrel.h"
#include "compiler/compiler.h"
#include "compiler/ast.h"
#include <sstream>
#include <string>
#include "utils.h"


using namespace SQCompilation;


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
