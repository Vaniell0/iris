/// @file   tests/test_channel.cpp
/// @brief  Tests for Channel and emit/recv wiring.

#include <gtest/gtest.h>
#include <channel.hpp>
#include <backend.hpp>
#include <sdk.hpp>

// ── Minimal backend for wiring tests ─────────────────────────────────────────

struct EchoBackend {
    iris::Channel* in_  = nullptr;
    iris::Channel* out_ = nullptr;

    std::string_view         runtime_name()              const { return "echo"; }
    bool                     can_handle(const iris::TypeDescriptor&) const { return true; }
    void                     emit(iris::IrisValue&& v)         { if (out_) out_->push(std::move(v)); }
    iris::IrisValue          recv()                            { return in_ ? in_->pop() : iris::IrisValue{}; }

    void set_input (iris::Channel* ch) { in_  = ch; }
    void set_output(iris::Channel* ch) { out_ = ch; }
};

static_assert(iris::Backend<EchoBackend>);

// ── Test type ─────────────────────────────────────────────────────────────────

struct Token { int32_t id; };
IRIS_TYPE(Token, IRIS_FIELD(Token, id))

// ── Channel unit tests ────────────────────────────────────────────────────────

TEST(Channel, PushPop) {
    iris::Channel ch;
    ch.push(iris::wrap(Token{42}));
    auto v = ch.pop();
    EXPECT_EQ(iris::unwrap<Token>(v).id, 42);
}

TEST(Channel, TryPopEmpty) {
    iris::Channel ch;
    EXPECT_FALSE(ch.try_pop().has_value());
}

TEST(Channel, TryPopPresent) {
    iris::Channel ch;
    ch.push(iris::wrap(Token{7}));
    auto opt = ch.try_pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(iris::unwrap<Token>(*opt).id, 7);
}

TEST(Channel, FifoOrder) {
    iris::Channel ch;
    for (int i = 0; i < 5; ++i)
        ch.push(iris::wrap(Token{i}));
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(iris::unwrap<Token>(ch.pop()).id, i);
}

// ── emit / recv wiring ────────────────────────────────────────────────────────

TEST(Channel, EmitRecvRoundTrip) {
    iris::Channel  ch;
    EchoBackend    src, dst;
    src.set_output(&ch);
    dst.set_input(&ch);

    src.emit(iris::wrap(Token{99}));
    auto v = dst.recv();
    EXPECT_EQ(iris::unwrap<Token>(v).id, 99);
}

TEST(Channel, EmitWithoutOutputIsNoop) {
    EchoBackend b;
    // no output channel connected — must not crash
    b.emit(iris::wrap(Token{0}));
}

TEST(Channel, RecvWithoutInputReturnsEmpty) {
    EchoBackend b;
    auto v = b.recv();
    EXPECT_EQ(v.type_id, 0u);
}
