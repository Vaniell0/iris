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

struct FieldRef  { std::string name; Loc loc; };   // size, name, score
struct IntLit    { int64_t  value;  Loc loc; };
struct FloatLit  { double   value;  Loc loc; };
struct StrLit    { std::string value; Loc loc; };
struct BoolLit   { bool     value;  Loc loc; };

struct BinOp;
struct UnOp;

using Expr = std::variant<
    FieldRef, IntLit, FloatLit, StrLit, BoolLit,
    std::unique_ptr<BinOp>, std::unique_ptr<UnOp>
>;

enum class BinOpKind { Eq, Ne, Lt, Le, Gt, Ge, And, Or,
                       Contains, StartsWith, EndsWith, Matches };
struct BinOp { BinOpKind op; Expr lhs; Expr rhs; Loc loc; };
struct UnOp  { Expr operand; Loc loc; };  // !

// ── Backend reference (@os.ls, @java, @ipc) ──────────────────────────────────

struct BackendRef {
    std::string ns;      // "os", "java", "ipc", or custom plugin name
    std::string op;      // "ls", "ps", "run", "exec", "" for leaf backends
    std::string config;  // content of (), if present; empty if no parens
    Loc         loc;
};

// ── Pipeline stages ───────────────────────────────────────────────────────────

struct FilterStage  { Expr predicate; Loc loc; };
struct SortStage    { std::string field; bool desc; Loc loc; };
struct MapStage     { std::vector<std::string> fields; Loc loc; };
struct SelectStage  { std::string field; Loc loc; };
struct HeadStage    { int64_t n; Loc loc; };
struct CollectStage { Loc loc; };
struct PrintStage   { Loc loc; };
struct WriteStage   { std::string path; Loc loc; };
struct BackendStage { BackendRef ref; };       // @ipc, @java("m"), @os.ls("/")
struct TypeDeclStage {};                       // placeholder; not a real stage

using Stage = std::variant<
    FilterStage, SortStage, MapStage, SelectStage,
    HeadStage, CollectStage, PrintStage, WriteStage,
    BackendStage
>;

// ── Statements ────────────────────────────────────────────────────────────────

struct Pipeline {
    BackendRef         source;   // first stage is always a source backend
    std::vector<Stage> stages;   // transform / sink stages
    Loc                loc;
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
    std::vector<Pipeline> arms;
    std::optional<Stage>  join;  // e.g. @java("Collector.merge") after &
    bool                  fire_and_forget;
    Loc                   loc;
};

using Statement = std::variant<LetStmt, Pipeline, TypeDecl, ParallelStmt>;

struct Program {
    std::vector<Statement> stmts;
};

} // namespace iris::irsh
