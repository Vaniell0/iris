/// @file   tests/test_os.cpp
/// @brief  Smoke tests for iris::os commands.

#include <gtest/gtest.h>
#include <backend/os.hpp>
#include <unistd.h>

// ── ls ────────────────────────────────────────────────────────────────────────

TEST(OsCommands, LsReturnsEntries) {
    auto result = iris::os::ls(".");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
    for (const auto& v : *result)
        EXPECT_EQ(v.type_id, iris::type_id_of<DirEntry>());
}

TEST(OsCommands, LsDirEntryFields) {
    auto result = iris::os::ls(".");
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->empty());
    const auto& e = iris::unwrap<DirEntry>((*result)[0]);
    EXPECT_GT(std::strlen(e.name), 0u);
}

TEST(OsCommands, LsReturnsErrorForMissingPath) {
    auto result = iris::os::ls("/nonexistent_path_iris_test");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), iris::os::OsError::NotFound);
}

// ── ps ────────────────────────────────────────────────────────────────────────

TEST(OsCommands, PsReturnsProcesses) {
    auto result = iris::os::ps();
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
    for (const auto& v : *result)
        EXPECT_EQ(v.type_id, iris::type_id_of<ProcEntry>());
}

TEST(OsCommands, PsHasOurPid) {
    auto result = iris::os::ps();
    ASSERT_TRUE(result.has_value());
    pid_t our  = getpid();
    bool found = false;
    for (const auto& v : *result) {
        const auto& e = iris::unwrap<ProcEntry>(v);
        if (e.pid == static_cast<int32_t>(our)) { found = true; break; }
    }
    EXPECT_TRUE(found) << "our pid " << our << " not found in ps() output";
}

// ── env ───────────────────────────────────────────────────────────────────────

TEST(OsCommands, EnvReturnsVars) {
    auto result = iris::os::env();
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
    for (const auto& v : *result)
        EXPECT_EQ(v.type_id, iris::type_id_of<EnvEntry>());
}

TEST(OsCommands, EnvHasPath) {
    auto result = iris::os::env();
    ASSERT_TRUE(result.has_value());
    bool found = false;
    for (const auto& v : *result) {
        const auto& e = iris::unwrap<EnvEntry>(v);
        if (std::string_view(e.key) == "PATH") { found = true; break; }
    }
    EXPECT_TRUE(found) << "PATH not found in env() output";
}

// ── OsBackend lazy streaming ──────────────────────────────────────────────────

TEST(OsBackend, LazyFirstRecv) {
    // First recv() on a fresh PsStream should return a valid ProcEntry
    // without reading all of /proc.
    auto backend = iris::OsBackend::ps();
    auto v = backend.recv();
    EXPECT_NE(v.type_id, 0u);
    EXPECT_EQ(v.type_id, iris::type_id_of<ProcEntry>());
}

TEST(OsBackend, LazyLsFirstEntry) {
    auto backend = iris::OsBackend::ls(".");
    auto v = backend.recv();
    EXPECT_NE(v.type_id, 0u);
    EXPECT_EQ(v.type_id, iris::type_id_of<DirEntry>());
}

TEST(OsBackend, LazyEnvFirstEntry) {
    auto backend = iris::OsBackend::env();
    auto v = backend.recv();
    EXPECT_NE(v.type_id, 0u);
    EXPECT_EQ(v.type_id, iris::type_id_of<EnvEntry>());
}

TEST(OsBackend, LazyDrainEqualsEager) {
    // Full lazy drain should match eager ps() count
    auto eager = iris::os::ps();
    ASSERT_TRUE(eager.has_value());

    auto backend = iris::OsBackend::ps();
    size_t lazy_count = 0;
    while (backend.recv().type_id != 0) ++lazy_count;

    EXPECT_EQ(lazy_count, eager->size());
}

TEST(OsBackend, CStrFieldsInDirEntry) {
    auto result = iris::os::ls(".");
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->empty());
    const auto& e = iris::unwrap<DirEntry>((*result)[0]);
    // name field must be a valid null-terminated string (CStr semantics)
    EXPECT_GT(std::strlen(e.name), 0u);
    EXPECT_LT(std::strlen(e.name), sizeof(e.name));
}
