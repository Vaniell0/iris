#pragma once
#include "ast.hpp"
#include "../lexer/token.hpp"
#include "../backend/backend_registry.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace iris::irsh {

// ── ImportTable ───────────────────────────────────────────────────────────────
//
// Snapshot of (ns, op, as_source, as_stage, config_kind) built from
// Session.imports() + BackendRegistry at each parse site.
// The parser uses config_kind to dispatch config parsing without knowing op names.

struct ImportedOp {
    std::string              ns;
    std::string              op;
    bool                     as_source;
    bool                     as_stage;
    IrshBackend::ConfigKind  config = IrshBackend::ConfigKind::None;
};
using ImportTable = std::vector<ImportedOp>;


struct ParseError {
    Loc         loc;
    std::string msg;
};

struct ParseResult {
    Program              program;
    std::vector<ParseError> errors;
    bool ok() const { return errors.empty(); }
};

class Parser {
public:
    // imports — ImportTable built by make_import_table(); empty = no bare-word sugar.
    explicit Parser(std::vector<Token> tokens, ImportTable imports = {});
    ParseResult parse();

private:
    std::vector<Token> tokens_;
    size_t             pos_ = 0;
    ImportTable        imports_;

    Token&      peek(size_t ahead = 0);
    Token&      advance();
    bool        check(TokenKind) const;
    bool        match(TokenKind);
    Token&      expect(TokenKind, std::string_view msg);

    Statement   parse_statement();
    LetStmt     parse_let();
    TypeDecl    parse_type_decl();
    ImportStmt  parse_import();
    Pipeline    parse_pipeline();
    BackendCall   parse_source();   // @ns.op or import-table shorthand
    BackendCall   parse_stage();    // returns BackendCall for any stage form
    BackendConfig parse_config(IrshBackend::ConfigKind kind, Loc loc);
    ExecArgs      parse_exec_args();

    // Consumes Ident(.token)* after @; returns {ns, op}.
    // Single segment (no dot) → {ns=segment, op=""}.
    std::pair<std::string, std::string> parse_dotted_path();

    Expr        parse_expr();
    Expr        parse_or();
    Expr        parse_and();
    Expr        parse_comparison();
    Expr        parse_unary();
    Expr        parse_primary();

    void        emit_error(std::string msg);
    std::vector<ParseError> errors_;
};

} // namespace iris::irsh
