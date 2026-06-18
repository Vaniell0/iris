/// @file   tests/test_execution.cpp

#include <gtest/gtest.h>
#include <sdk/cpp/iris.hpp>
#include <backend/fn.hpp>
#include <execution.hpp>

struct Num { int32_t val; };
IRIS_TYPE(Num, IRIS_FIELD(Num, val))

// ── Helpers ───────────────────────────────────────────────────────────────────

static auto make_double() {
    return iris::FnBackend([](iris::IrisValue&& v) -> iris::IrisValue {
        auto n = iris::unwrap<Num>(v); n.val *= 2; return iris::wrap(n);
    });
}
static auto make_add_one() {
    return iris::FnBackend([](iris::IrisValue&& v) -> iris::IrisValue {
        auto n = iris::unwrap<Num>(v); n.val += 1; return iris::wrap(n);
    });
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(Execution, JustYieldsValue) {
    auto result = iris::sync_wait(iris::just(iris::wrap(Num{7})));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(iris::unwrap<Num>(*result).val, 7);
}

TEST(Execution, ViaPipedFn) {
    // double | add_one: 5 → 10 → 11
    auto pipeline = make_double() | make_add_one();
    auto result = iris::sync_wait(
        iris::just(iris::wrap(Num{5})) | iris::via(pipeline)
    );
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(iris::unwrap<Num>(*result).val, 11);
}

TEST(Execution, ThenChain) {
    // double | add_one → then(negate): 5 → 11 → -11
    auto pipeline = make_double() | make_add_one();
    auto result = iris::sync_wait(
        iris::just(iris::wrap(Num{5}))
        | iris::via(pipeline)
        | iris::then([](iris::IrisValue v) -> iris::IrisValue {
              auto n = iris::unwrap<Num>(v); n.val = -n.val; return iris::wrap(n);
          })
    );
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(iris::unwrap<Num>(*result).val, -11);
}

TEST(Execution, ScheduleOnPool) {
    // double | add_one → schedule_on → then(+100): 5 → 11 → 111
    iris::thread_pool tp(2);
    auto pipeline = make_double() | make_add_one();
    auto result = iris::sync_wait(
        iris::just(iris::wrap(Num{5}))
        | iris::via(pipeline)
        | iris::schedule_on(tp)
        | iris::then([](iris::IrisValue v) -> iris::IrisValue {
              auto n = iris::unwrap<Num>(v); n.val += 100; return iris::wrap(n);
          })
    );
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(iris::unwrap<Num>(*result).val, 111);
}
