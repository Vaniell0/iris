/// @file   tests/test_ipc_backend.cpp

#include <gtest/gtest.h>
#include <backend/ipc.hpp>
#include <sdk/cpp/iris.hpp>
#include <sys/socket.h>

struct IpcPoint { int32_t x, y; };
IRIS_TYPE(IpcPoint, IRIS_FIELD(IpcPoint, x), IRIS_FIELD(IpcPoint, y))

TEST(IpcBackend, SatisfiesBackendConcept) {
    static_assert(iris::Backend<iris::IpcBackend>);
}

TEST(IpcBackend, EmitRecvRoundTrip) {
    int fds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    iris::IpcBackend sender   = iris::IpcBackend::from_fd(fds[0]);
    iris::IpcBackend receiver = iris::IpcBackend::from_fd(fds[1]);

    IpcPoint pt{17, 42};
    iris::IrisValue v;
    v.type_id = iris::type_id_of<IpcPoint>();
    v.payload = iris::IrisBuffer::from(&pt, sizeof(pt));
    sender.emit(std::move(v));

    iris::IrisValue got = receiver.recv();
    ASSERT_NE(got.type_id, 0u);
    EXPECT_EQ(got.type_id, iris::type_id_of<IpcPoint>());
    ASSERT_TRUE(got.is_raw());
    ASSERT_EQ(got.raw().size(), sizeof(IpcPoint));

    IpcPoint out{};
    std::memcpy(&out, got.raw().data(), sizeof(out));
    EXPECT_EQ(out.x, 17);
    EXPECT_EQ(out.y, 42);
}

TEST(IpcBackend, EmptyRecvOnClosedPeer) {
    int fds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    iris::IpcBackend receiver = iris::IpcBackend::from_fd(fds[1]);
    ::close(fds[0]);  // close the sender side

    iris::IrisValue got = receiver.recv();
    EXPECT_EQ(got.type_id, 0u);
}
