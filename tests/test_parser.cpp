#include <gtest/gtest.h>
#include "irish/parser/parser.hpp"
#include "irish/parser/import_table.hpp"
#include "irish/lexer/lexer.hpp"
#include "irish/backend/os_irsh.hpp"
#include "irish/backend/base_irsh.hpp"
#include "irish/session/session.hpp"
#include <registry.hpp>
using namespace iris::irsh;

static ImportTable& default_imports() {
    static BackendRegistry reg;
    static Session         session;
    static bool            built = false;
    if (!built) {
        reg.register_backend(std::make_unique<BaseIrshBackend>(
            iris::TypeRegistry::global(), session));
        reg.register_backend(std::make_unique<OsIrshBackend>());
        reg.freeze();
        built = true;
    }
    static ImportTable tbl = make_import_table(reg, session);
    return tbl;
}

static ParseResult parse(std::string_view src) {
    Lexer l{src};
    Parser p{l.tokenise(), default_imports()};
    return p.parse();
}

// Convenience: extract the sole pipeline from a parsed result
static const Pipeline& pipeline(const ParseResult& r) {
    return std::get<Pipeline>(r.program.stmts[0]);
}

// ── Stage syntax ──────────────────────────────────────────────────────────────

TEST(Parser, FilterNoParen) {
    auto r = parse("@os.ls | filter size > 1024");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& stages = pipeline(r).stages;
    ASSERT_EQ(stages.size(), 1u);
    EXPECT_EQ(stages[0].ns, "base");
    EXPECT_EQ(stages[0].op, "filter");
    EXPECT_TRUE(std::holds_alternative<Expr>(stages[0].config));
}

TEST(Parser, SortByColon) {
    auto r = parse("@os.ls | sort by: size desc");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& bc = pipeline(r).stages[0];
    EXPECT_EQ(bc.op, "sort");
    auto& sa = std::get<SortArg>(bc.config);
    EXPECT_EQ(sa.field, "size");
    EXPECT_TRUE(sa.desc);
}

TEST(Parser, SortByColonNoDesc) {
    auto r = parse("@os.ls | sort by: size");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& sa = std::get<SortArg>(pipeline(r).stages[0].config);
    EXPECT_EQ(sa.field, "size");
    EXPECT_FALSE(sa.desc);
}

TEST(Parser, SortNoBy) {
    auto r = parse("@os.ls | sort size");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& sa = std::get<SortArg>(pipeline(r).stages[0].config);
    EXPECT_EQ(sa.field, "size");
    EXPECT_FALSE(sa.desc);
}

TEST(Parser, SelectNoParen) {
    auto r = parse("@os.ls | select name");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& bc = pipeline(r).stages[0];
    EXPECT_EQ(bc.op, "select");
    auto& e  = std::get<Expr>(bc.config);
    auto& fr = std::get<FieldRef>(e);
    EXPECT_EQ(fr.name, "name");
}

TEST(Parser, MapBraces) {
    auto r = parse("@os.ls | map { name, size }");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& bc     = pipeline(r).stages[0];
    EXPECT_EQ(bc.op, "map");
    auto& fields = std::get<std::vector<std::string>>(bc.config);
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0], "name");
    EXPECT_EQ(fields[1], "size");
}

// ── Source shorthand ──────────────────────────────────────────────────────────

TEST(Parser, ShorthandLsString) {
    auto r = parse("ls \".\" | head 5");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    EXPECT_EQ(pipeline(r).source.ns, "os");
    EXPECT_EQ(pipeline(r).source.op, "ls");
}

TEST(Parser, ShorthandLsPathLiteral) {
    auto r = parse("ls /var/log | head 5");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& src = pipeline(r).source;
    EXPECT_EQ(src.ns, "os");
    EXPECT_EQ(src.op, "ls");
    EXPECT_FALSE(std::holds_alternative<std::monostate>(src.config));
}

TEST(Parser, ShorthandPs) {
    auto r = parse("ps | head 3");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    EXPECT_EQ(pipeline(r).source.ns, "os");
    EXPECT_EQ(pipeline(r).source.op, "ps");
}

TEST(Parser, ShorthandEnv) {
    auto r = parse("env | head 3");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    EXPECT_EQ(pipeline(r).source.op, "env");
}

// Unrecognized bare word → variable reference (not an error at parse time)
TEST(Parser, UnknownBareWordIsVar) {
    auto r = parse("run_cmd | head 5");
    // 'run_cmd' is not a registered source op, becomes _var ref — no parse error
    ASSERT_TRUE(r.ok());
    auto& src = std::get<Pipeline>(r.program.stmts[0]).source;
    EXPECT_EQ(src.ns, "_var");
    EXPECT_EQ(src.op, "run_cmd");
}

// ── `write` stage ─────────────────────────────────────────────────────────────

TEST(Parser, WriteNoParen) {
    auto r = parse("@os.ls | write \"out.txt\"");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& bc   = pipeline(r).stages[0];
    EXPECT_EQ(bc.op, "write");
    auto& path = std::get<std::string>(bc.config);
    EXPECT_EQ(path, "out.txt");
}

TEST(Parser, WritePathLiteral) {
    auto r = parse("@os.ls | write /tmp/out");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& bc   = pipeline(r).stages[0];
    EXPECT_EQ(bc.op, "write");
    auto& path = std::get<std::string>(bc.config);
    EXPECT_EQ(path, "/tmp/out");
}

// ── Operator precedence ───────────────────────────────────────────────────────

TEST(Parser, FilterPrecedenceAnd) {
    auto r = parse("@os.ps | filter name == \"init\" && pid > 0");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& bc  = pipeline(r).stages[0];
    auto& pred = std::get<Expr>(bc.config);
    auto& top  = std::get<std::shared_ptr<BinOp>>(pred);
    EXPECT_EQ(top->op, BinOpKind::And)  << "top node must be &&";
    auto& lhs = std::get<std::shared_ptr<BinOp>>(top->lhs);
    EXPECT_EQ(lhs->op, BinOpKind::Eq)   << "left of && must be ==";
    auto& rhs = std::get<std::shared_ptr<BinOp>>(top->rhs);
    EXPECT_EQ(rhs->op, BinOpKind::Gt)   << "right of && must be >";
}

TEST(Parser, FilterPrecedenceOr) {
    auto r = parse("@os.ls | filter size > 1024 || name == \"core\"");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& pred = std::get<Expr>(pipeline(r).stages[0].config);
    auto& top  = std::get<std::shared_ptr<BinOp>>(pred);
    EXPECT_EQ(top->op, BinOpKind::Or)   << "top node must be ||";
}

TEST(Parser, FilterPrecedenceOrOverAnd) {
    auto r = parse("@os.ls | filter size > 0 && mode > 0 || size > 1024 && mode > 0");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& pred = std::get<Expr>(pipeline(r).stages[0].config);
    auto& top  = std::get<std::shared_ptr<BinOp>>(pred);
    EXPECT_EQ(top->op, BinOpKind::Or)  << "|| is lower precedence than &&";
    auto& l    = std::get<std::shared_ptr<BinOp>>(top->lhs);
    EXPECT_EQ(l->op, BinOpKind::And)   << "left child of || must be &&";
    auto& r2   = std::get<std::shared_ptr<BinOp>>(top->rhs);
    EXPECT_EQ(r2->op, BinOpKind::And)  << "right child of || must be &&";
}

TEST(Parser, FilterNot) {
    auto r = parse("@os.ls | filter !(name == \"tmp\")");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& pred = std::get<Expr>(pipeline(r).stages[0].config);
    EXPECT_TRUE(std::holds_alternative<std::shared_ptr<UnOp>>(pred));
}

// ── Snake_case predicates ─────────────────────────────────────────────────────

TEST(Parser, Contains) {
    auto r = parse("@os.ls | filter name contains \"lib\"");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& pred = std::get<Expr>(pipeline(r).stages[0].config);
    auto& bin  = std::get<std::shared_ptr<BinOp>>(pred);
    EXPECT_EQ(bin->op, BinOpKind::Contains);
}

TEST(Parser, StartsWith) {
    auto r = parse("@os.ls | filter name starts_with \"lib\"");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& pred = std::get<Expr>(pipeline(r).stages[0].config);
    auto& bin  = std::get<std::shared_ptr<BinOp>>(pred);
    EXPECT_EQ(bin->op, BinOpKind::StartsWith);
}

TEST(Parser, EndsWith) {
    auto r = parse("@os.ls | filter name ends_with \".so\"");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& pred = std::get<Expr>(pipeline(r).stages[0].config);
    auto& bin  = std::get<std::shared_ptr<BinOp>>(pred);
    EXPECT_EQ(bin->op, BinOpKind::EndsWith);
}

TEST(Parser, Matches) {
    auto r = parse("@os.ls | filter name matches \"lib.*\\.so\"");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& pred = std::get<Expr>(pipeline(r).stages[0].config);
    auto& bin  = std::get<std::shared_ptr<BinOp>>(pred);
    EXPECT_EQ(bin->op, BinOpKind::Matches);
}

TEST(Parser, CamelCaseStartsWithRejected) {
    auto r = parse("@os.ls | filter name startsWith \"lib\"");
    // startsWith is parsed as a FieldRef then the pipeline hits next token
    if (r.ok()) {
        auto& pred = std::get<Expr>(pipeline(r).stages[0].config);
        if (auto* bin = std::get_if<std::shared_ptr<BinOp>>(&pred))
            EXPECT_NE((*bin)->op, BinOpKind::StartsWith)
                << "camelCase must not produce StartsWith";
    }
}

// ── Misc ──────────────────────────────────────────────────────────────────────

TEST(Parser, HeadNoParen) {
    auto r = parse("@os.ls | head 5");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& bc = pipeline(r).stages[0];
    EXPECT_EQ(bc.op, "head");
    auto& e  = std::get<Expr>(bc.config);
    auto& il = std::get<IntLit>(e);
    EXPECT_EQ(il.value, 5);
}

TEST(Parser, LetBinding) {
    auto r = parse("let logs = @os.ls");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& let = std::get<LetStmt>(r.program.stmts[0]);
    EXPECT_EQ(let.name, "logs");
    EXPECT_EQ(let.rhs.source.op, "ls");
}

TEST(Parser, ChainedStages) {
    auto r = parse("@os.ls | filter size > 0 | sort by: size | head 10 | select name");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    EXPECT_EQ(pipeline(r).stages.size(), 4u);
}

// ── New features ──────────────────────────────────────────────────────────────

TEST(Parser, LitSource) {
    auto r = parse("@base.lit(\"hello\") | print");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    EXPECT_EQ(pipeline(r).source.ns, "base");
    EXPECT_EQ(pipeline(r).source.op, "lit");
    EXPECT_EQ(std::get<std::string>(pipeline(r).source.config), "hello");
}

TEST(Parser, StringLiteralSugar) {
    auto r = parse("\"hello world\" | print");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    EXPECT_EQ(pipeline(r).source.ns, "base");
    EXPECT_EQ(pipeline(r).source.op, "lit");
}

TEST(Parser, FallbackVal) {
    auto r = parse("@os.ls | head 1 ?? \"none\"");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    EXPECT_TRUE(pipeline(r).fallback_val.has_value());
}

TEST(Parser, FallbackPipe) {
    auto r = parse("@os.ls \"/missing\" ?| @os.ls \"/tmp\"");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    EXPECT_TRUE(pipeline(r).fallback_pipe != nullptr);
}

TEST(Parser, ParallelStatement) {
    auto r = parse("@os.ps | head 3 & @os.ls | head 3");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& par = std::get<ParallelStmt>(r.program.stmts[0]);
    EXPECT_EQ(par.arms.size(), 2u);
    EXPECT_FALSE(par.fire_and_forget);
}

TEST(Parser, FireAndForget) {
    auto r = parse("@os.ps | head 1 &! @os.ls | head 1");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& par = std::get<ParallelStmt>(r.program.stmts[0]);
    EXPECT_TRUE(par.fire_and_forget);
}

TEST(Parser, ExecDSL) {
    // simple word args (no dots — dotted filenames need quoting in exec)
    auto r = parse("exec( grep foo bar ) | head 5");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    EXPECT_EQ(pipeline(r).source.ns, "os");
    EXPECT_EQ(pipeline(r).source.op, "exec");
    auto& args = std::get<ExecArgs>(pipeline(r).source.config);
    EXPECT_EQ(args.size(), 3u);
    EXPECT_FALSE(args[0].is_var);
    EXPECT_EQ(args[0].text, "grep");
}

TEST(Parser, ExecDSLWithVar) {
    auto r = parse("exec( grep -n $pattern CMakeLists.txt )");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& args = std::get<ExecArgs>(pipeline(r).source.config);
    EXPECT_TRUE(args[2].is_var);
    EXPECT_EQ(args[2].text, "pattern");
}

TEST(Parser, ParseStageTypeName) {
    auto r = parse("@os.ls | parse(DirEntry)");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& bc = pipeline(r).stages[0];
    EXPECT_EQ(bc.op, "parse");
    EXPECT_EQ(std::get<std::string>(bc.config), "DirEntry");
}

TEST(Parser, ParseStageBareIdent) {
    auto r = parse("@os.ls | parse DirEntry");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    EXPECT_EQ(std::get<std::string>(pipeline(r).stages[0].config), "DirEntry");
}

TEST(Parser, WriteStage) {
    auto r = parse("@os.ls | write \"/tmp/out.txt\"");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    EXPECT_EQ(std::get<std::string>(pipeline(r).stages[0].config), "/tmp/out.txt");
}

TEST(Parser, DollarExprInHead) {
    auto r = parse("@os.ls | head $n");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& e = std::get<Expr>(pipeline(r).stages[0].config);
    EXPECT_TRUE(std::holds_alternative<DollarExpr>(e));
}

TEST(Parser, LetWithCollect) {
    auto r = parse("let files = @os.ls | collect");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& let = std::get<LetStmt>(r.program.stmts[0]);
    EXPECT_EQ(let.rhs.stages.back().op, "collect");
}

// ── PATH exec sugar (S1) ──────────────────────────────────────────────────────

// Multi-word bare command → @os.exec with ExecArgs
TEST(Parser, MultiWordPathCommand) {
    auto r = parse("nix profile list");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    ASSERT_EQ(r.program.stmts.size(), 1u);
    auto& src = pipeline(r).source;
    EXPECT_EQ(src.ns, "os");
    EXPECT_EQ(src.op, "exec");
    auto& args = std::get<ExecArgs>(src.config);
    ASSERT_EQ(args.size(), 3u);
    EXPECT_EQ(args[0].text, "nix");
    EXPECT_EQ(args[1].text, "profile");
    EXPECT_EQ(args[2].text, "list");
}

// Flags and path args
TEST(Parser, MultiWordWithFlags) {
    auto r = parse("git log --oneline ./repo");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& args = std::get<ExecArgs>(pipeline(r).source.config);
    ASSERT_EQ(args.size(), 4u);
    EXPECT_EQ(args[0].text, "git");
    EXPECT_EQ(args[1].text, "log");
    EXPECT_EQ(args[2].text, "--oneline");
    EXPECT_EQ(args[3].text, "./repo");
}

// Piped multi-word: command + args, then stage
TEST(Parser, MultiWordPiped) {
    auto r = parse("nix profile list | head 5");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& src = pipeline(r).source;
    EXPECT_EQ(src.ns, "os");
    EXPECT_EQ(src.op, "exec");
    auto& args = std::get<ExecArgs>(src.config);
    ASSERT_EQ(args.size(), 3u);
    EXPECT_EQ(pipeline(r).stages[0].op, "head");
}

// Single bare word with no args → still _var (session variable lookup)
TEST(Parser, SingleBareWordIsVar) {
    auto r = parse("myvar | head 3");
    ASSERT_TRUE(r.ok());
    auto& src = pipeline(r).source;
    EXPECT_EQ(src.ns, "_var");
    EXPECT_EQ(src.op, "myvar");
}

// Multi-word with $VAR expansion
TEST(Parser, MultiWordWithDollarVar) {
    auto r = parse("grep -n $pattern ./file");
    ASSERT_TRUE(r.ok()) << r.errors[0].msg;
    auto& args = std::get<ExecArgs>(pipeline(r).source.config);
    ASSERT_EQ(args.size(), 4u);
    EXPECT_FALSE(args[0].is_var);
    EXPECT_EQ(args[0].text, "grep");
    EXPECT_FALSE(args[1].is_var);
    EXPECT_EQ(args[1].text, "-n");
    EXPECT_TRUE(args[2].is_var);
    EXPECT_EQ(args[2].text, "pattern");
    EXPECT_FALSE(args[3].is_var);
    EXPECT_EQ(args[3].text, "./file");
}

// ── Error recovery ────────────────────────────────────────────────────────────

// Non-ASCII (e.g. Cyrillic) → single parse error, not one per byte
TEST(Parser, NonAsciiSingleError) {
    auto r = parse("ещлуш");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.errors.size(), 1u) << "expected exactly one error for non-ASCII input";
    EXPECT_EQ(r.errors[0].msg, "unexpected character in input");
}

// After error, recovery continues and next valid stmt parses
TEST(Parser, RecoveryAfterError) {
    auto r = parse("ещё; @os.ls | head 3");
    // First token is Error (non-ASCII) → single error; then @os.ls parses OK
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.errors.size(), 1u);
    // The valid statement should still be present
    EXPECT_GE(r.program.stmts.size(), 1u);
}
