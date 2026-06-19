#include <gtest/gtest.h>
#include "irish/parser/parser.hpp"
#include "irish/lexer/lexer.hpp"
using namespace iris::irsh;

static ParseResult parse(std::string_view src) {
    Lexer l{src};
    Parser p{l.tokenise()};
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

// run/exec/lines are NOT shorthands — require explicit @os. prefix
TEST(Parser, RunNotShorthand) {
    auto r = parse("run \"git log\" | head 5");
    EXPECT_FALSE(r.ok());
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
