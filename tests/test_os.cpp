/// @file   tests/test_os.cpp
/// @brief  Smoke tests for iris::os commands.

#include <gtest/gtest.h>
#include <os.hpp>
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
