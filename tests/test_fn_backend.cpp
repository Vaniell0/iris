/// @file   tests/test_fn_backend.cpp

#include <gtest/gtest.h>
#include <sdk.hpp>
#include <fn_backend.hpp>
#include <channel.hpp>

struct Token { int32_t value; };
IRIS_TYPE(Token, IRIS_FIELD(Token, value))

TEST(FnBackend, TransformViaChannel) {
    iris::Channel in, out;

    // double the value
    auto fn = iris::FnBackend([](iris::IrisValue&& v) -> iris::IrisValue {
        auto t = iris::unwrap<Token>(v);
        t.value *= 2;
        return iris::wrap(t);
    });
    fn.set_input(&in);
    fn.set_output(&out);

    Token src{21};
    in.push(iris::wrap(src));

    iris::IrisValue result = fn.recv();
    ASSERT_TRUE(result.is_raw());
    EXPECT_EQ(iris::unwrap<Token>(result).value, 42);
}

TEST(FnBackend, EmitPushesToOutput) {
    iris::Channel out;
    auto fn = iris::FnBackend([](iris::IrisValue&& v) { return v; });
    fn.set_output(&out);

    fn.emit(iris::wrap(Token{7}));
    auto opt = out.try_pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(iris::unwrap<Token>(*opt).value, 7);
}

TEST(FnBackend, RecvWithoutInputReturnsEmpty) {
    auto fn = iris::FnBackend([](iris::IrisValue&& v) { return v; });
    EXPECT_EQ(fn.recv().type_id, 0u);
}

TEST(FnBackend, SatisfiesBackendConcept) {
    static_assert(iris::Backend<iris::FnBackend<std::function<iris::IrisValue(iris::IrisValue&&)>>>);
}
