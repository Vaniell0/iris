/// @file   tests/test_iris_handle.cpp

#include <gtest/gtest.h>
#include <sdk/iris_backend.h>
#include <sdk/cpp/iris.hpp>
#include <registry.hpp>
#include <cstring>

struct HPoint { int32_t x, y; };
IRIS_TYPE(HPoint,
    IRIS_FIELD(HPoint, x),
    IRIS_FIELD(HPoint, y)
)

class IrisHandleTest : public ::testing::Test {
protected:
    iris_backend_t* h = nullptr;

    void SetUp() override {
        h = iris_backend_java_create();
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(h->vtable->connect(h, nullptr), 0);
    }
    void TearDown() override {
        h->vtable->disconnect(h);
        iris_backend_destroy(h);
    }
};

TEST_F(IrisHandleTest, EmitRecvRoundTrip) {
    iris::TypeId id = iris::type_id_of<HPoint>();

    HPoint src{11, 22};
    uint8_t payload[sizeof(HPoint)];
    std::memcpy(payload, &src, sizeof(src));

    h->vtable->emit(h, static_cast<uint64_t>(id), payload, sizeof(payload));

    uint64_t out_id = 0;
    uint8_t  out_payload[256]{};
    size_t   n = h->vtable->recv(h, &out_id, out_payload, sizeof(out_payload));

    EXPECT_EQ(n, sizeof(HPoint));
    EXPECT_EQ(static_cast<iris::TypeId>(out_id), id);

    HPoint dst{};
    std::memcpy(&dst, out_payload, sizeof(dst));
    EXPECT_EQ(dst.x, 11);
    EXPECT_EQ(dst.y, 22);
}

TEST_F(IrisHandleTest, RecvEmptyReturnsZero) {
    uint64_t id = 0;
    uint8_t  buf[64]{};
    EXPECT_EQ(h->vtable->recv(h, &id, buf, sizeof(buf)), 0u);
}
