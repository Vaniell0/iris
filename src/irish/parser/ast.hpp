#pragma once
#include "../lexer/token.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace iris::irsh {

// ── Source location ───────────────────────────────────────────────────────────

struct Loc { uint32_t line, col; };

// ── Filter predicate (expression tree) ───────────────────────────────────────

struct FieldRef  { std::string name; Loc loc; };
struct IntLit    { int64_t  value;  Loc loc; };
struct FloatLit  { double   value;  Loc loc; };
struct StrLit    { std::string value; Loc loc; };
struct BoolLit   { bool     value;  Loc loc; };

// $args[N] or $args.flag — resolved at runtime from script argv
struct DollarExpr {
    std::string name;  // variable name ("args")
    std::variant<std::monostate, int64_t, std::string> access;  // none, [N], .flag
    Loc loc;
};

struct BinOp;
struct UnOp;

using Expr = std::variant<
    FieldRef, IntLit, FloatLit, StrLit, BoolLit, DollarExpr,
    std::shared_ptr<BinOp>, std::shared_ptr<UnOp>
>;

enum class BinOpKind { Eq, Ne, Lt, Le, Gt, Ge, And, Or,
                       Contains, StartsWith, EndsWith, Matches };
struct BinOp { BinOpKind op; Expr lhs; Expr rhs; Loc loc; };
struct UnOp  { Expr operand; Loc loc; };

// ── Backend call config ───────────────────────────────────────────────────────
//
// Every @ns.op(config) carries exactly one of these config kinds.
// The backend's check() / stream() interprets it.
//
//   monostate         — no args:  @os.ps, @base.collect, @base.print
//   string            — one string arg: @os.ls("/var"), @base.write("f.txt")
//   Expr              — typed expr: @base.filter(size>0), @base.select(name),
//                                   @base.head(10)
//   vector<string>    — field list: @base.map({ name, size })
//   SortArg           — sort field + desc flag: @base.sort(size desc)

struct SortArg { std::string field; bool desc = false; };

// exec() DSL — safe argv construction without shell interpolation.
// Each word is either a literal or a $name variable reference.
struct ExecWord { bool is_var; std::string text; };
using ExecArgs = std::vector<ExecWord>;

using BackendConfig = std::variant<
    std::monostate,
    std::string,
    Expr,
    std::vector<std::string>,
    SortArg,
    ExecArgs
>;

// ── Backend call — the only stage node ───────────────────────────────────────
//
// Every pipeline stage is a BackendCall.  There are no FilterStage,
// SortStage, etc.  Adding @math.sum or @crypto.hash requires zero changes
// to the parser or checker — only a new IrshBackend registration.

struct BackendCall {
    std::string   ns;      // "os", "base", "java", "ipc", custom
    std::string   op;      // "ls", "filter", "sort", …; empty for leaf backends
    BackendConfig config;
    Loc           loc;
};

// Pipeline: source call + zero or more stage calls
using Stage = BackendCall;

// ── Statements ────────────────────────────────────────────────────────────────

struct Pipeline {
    BackendCall         source;
    std::vector<Stage>  stages;
    Loc                 loc;
    // ?? expr  — substitute a default value when the stream is empty
    std::optional<Expr>               fallback_val;
    // ?| pipeline — switch to alternate pipeline when the stream is empty
    std::shared_ptr<Pipeline>         fallback_pipe;
};

struct LetStmt {
    std::string name;
    Pipeline    rhs;
    Loc         loc;
};

struct TypeDecl {
    struct Field { std::string name; std::string kind; uint32_t size; Loc loc; };
    std::string        name;
    std::vector<Field> fields;
    Loc                loc;
};

struct ParallelStmt {
    std::vector<Pipeline>    arms;
    std::optional<BackendCall> join;
    bool                     fire_and_forget;
    Loc                      loc;
};

// import @ns — adds ns to session auto-imports at runtime
struct ImportStmt {
    std::string ns;
    Loc         loc;
};

using Statement = std::variant<LetStmt, Pipeline, TypeDecl, ParallelStmt, ImportStmt>;

struct Program {
    std::vector<Statement> stmts;
};

} // namespace iris::irsh
