#include <gtest/gtest.h>
#include "irish/checker/checker.hpp"
#include "irish/parser/parser.hpp"
#include "irish/parser/import_table.hpp"
#include "irish/lexer/lexer.hpp"
#include "irish/session/session.hpp"
#include "irish/backend/os_irsh.hpp"
#include "irish/backend/base_irsh.hpp"
#include <registry.hpp>
using namespace iris::irsh;

static TypedProgram check_src(std::string_view src) {
    iris::TypeRegistry::global().freeze();  // idempotent
    Session s;
    BackendRegistry reg;
    reg.register_backend(std::make_unique<BaseIrshBackend>(
        iris::TypeRegistry::global(), s));
    reg.register_backend(std::make_unique<OsIrshBackend>());
    Checker c{iris::TypeRegistry::global(), s.session_types(), reg};
    Lexer l{src};
    Parser p{l.tokenise(), make_import_table(reg, s)};
    auto pr = p.parse();
    if (!pr.ok()) {
        TypedProgram err;
        err.errors.push_back({pr.errors[0].loc, pr.errors[0].msg});
        return err;
    }
    return c.check(pr.program);
}

// ── Valid pipelines ───────────────────────────────────────────────────────────

TEST(Checker, ValidLsFilter) {
    auto r = check_src("@os.ls | filter size > 1024");
    EXPECT_TRUE(r.ok()) << r.errors[0].msg;
}

TEST(Checker, SortFieldExists) {
    auto r = check_src("@os.ls | sort by: size");
    EXPECT_TRUE(r.ok()) << r.errors[0].msg;
}

TEST(Checker, SelectKnownField) {
    auto r = check_src("@os.ls | select name");
    EXPECT_TRUE(r.ok()) << r.errors[0].msg;
}

TEST(Checker, PsFilterName) {
    auto r = check_src("@os.ps | filter name == \"init\"");
    EXPECT_TRUE(r.ok()) << r.errors[0].msg;
}

TEST(Checker, EnvFilter) {
    auto r = check_src("env | filter key starts_with \"PATH\"");
    EXPECT_TRUE(r.ok()) << r.errors[0].msg;
}

TEST(Checker, ShorthandLsFilter) {
    auto r = check_src("ls \".\" | filter size > 0");
    EXPECT_TRUE(r.ok()) << r.errors[0].msg;
}

TEST(Checker, ShorthandPsHead) {
    auto r = check_src("ps | head 5");
    EXPECT_TRUE(r.ok()) << r.errors[0].msg;
}

TEST(Checker, CompoundFilterPrecedence) {
    auto r = check_src("@os.ps | filter name == \"init\" && pid > 0");
    EXPECT_TRUE(r.ok()) << r.errors[0].msg;
}

TEST(Checker, ChainedStages) {
    auto r = check_src("@os.ls | filter size > 0 | sort by: size | head 10 | select name");
    EXPECT_TRUE(r.ok()) << r.errors[0].msg;
}

// ── Field errors caught at check time ─────────────────────────────────────────

TEST(Checker, BadFieldInFilter) {
    auto r = check_src("@os.ls | filter nofield == 1");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.errors[0].msg.find("nofield"), std::string::npos);
}

TEST(Checker, SortBadField) {
    auto r = check_src("@os.ls | sort by: nofield");
    EXPECT_FALSE(r.ok());
}

TEST(Checker, SelectBadField) {
    auto r = check_src("@os.ls | select nofield");
    EXPECT_FALSE(r.ok());
}

TEST(Checker, LsFilterWithPsField) {
    auto r = check_src("@os.ls | filter pid > 0");
    EXPECT_FALSE(r.ok());
}

TEST(Checker, PsFilterWithLsField) {
    auto r = check_src("@os.ps | filter size > 0");
    EXPECT_FALSE(r.ok());
}

// ── Let binding ───────────────────────────────────────────────────────────────

TEST(Checker, LetBinding) {
    auto r = check_src("let logs = @os.ls");
    EXPECT_TRUE(r.ok()) << r.errors[0].msg;
}

// ── map and types ─────────────────────────────────────────────────────────────

TEST(Checker, MapValidFields) {
    auto r = check_src("@os.ls | map { name, size }");
    EXPECT_TRUE(r.ok()) << (r.errors.empty() ? "" : r.errors[0].msg);
}

TEST(Checker, MapBadField) {
    auto r = check_src("@os.ls | map { nofield }");
    EXPECT_FALSE(r.ok());
}

TEST(Checker, TypesOp) {
    auto r = check_src("types");
    EXPECT_TRUE(r.ok()) << (r.errors.empty() ? "" : r.errors[0].msg);
}

TEST(Checker, TypeWithName) {
    auto r = check_src("@os.ls | type(DirEntry)");
    EXPECT_TRUE(r.ok()) << (r.errors.empty() ? "" : r.errors[0].msg);
}

TEST(Checker, TypeWithoutName) {
    auto r = check_src("@os.ls | type");
    EXPECT_FALSE(r.ok());
}
