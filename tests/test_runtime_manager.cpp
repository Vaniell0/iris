/// @file   tests/test_runtime_manager.cpp

#include <gtest/gtest.h>
#include <backend/runtime.hpp>
#include <backend/java/java_backend.hpp>

TEST(RuntimeManager, SingletonReturnsConsistentJvm) {
    auto& rm = iris::RuntimeManager::global();
    auto r1 = rm.acquire(); ASSERT_TRUE(r1.has_value());
    auto r2 = rm.acquire(); ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r1, *r2) << "acquire() must return the same JavaVM* every time";
}

TEST(RuntimeManager, EnvNotNull) {
    auto& rm = iris::RuntimeManager::global();
    [[maybe_unused]] auto r = rm.acquire();
    EXPECT_NE(rm.env(), nullptr);
}

TEST(RuntimeManager, TwoBackendsShareOneJvm) {
    iris::JavaBackend b1, b2;
    ASSERT_TRUE(b1.connect().has_value());
    ASSERT_TRUE(b2.connect().has_value());
    EXPECT_EQ(b1.jvm(), b2.jvm()) << "both backends must share the process JVM";
}
