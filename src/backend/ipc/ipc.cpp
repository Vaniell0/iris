/// @file   src/backend/ipc/ipc.cpp

#include <backend/ipc.hpp>
#include <cstring>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

namespace iris {

bool IpcBackend::write_all(const void* buf, std::size_t n) {
    const auto* p = static_cast<const uint8_t*>(buf);
    while (n > 0) {
        ssize_t w = ::write(fd_, p, n);
        if (w <= 0) return false;
        p += w; n -= static_cast<std::size_t>(w);
    }
    return true;
}

bool IpcBackend::read_all(void* buf, std::size_t n) {
    auto* p = static_cast<uint8_t*>(buf);
    while (n > 0) {
        ssize_t r = ::read(fd_, p, n);
        if (r <= 0) return false;
        p += r; n -= static_cast<std::size_t>(r);
    }
    return true;
}

static bool writev_all(int fd, struct iovec* iov, int niov) {
    while (niov > 0) {
        ssize_t n = ::writev(fd, iov, niov);
        if (n <= 0) return false;
        auto rem = static_cast<std::size_t>(n);
        while (rem > 0 && niov > 0) {
            if (rem >= iov[0].iov_len) {
                rem -= iov[0].iov_len;
                ++iov; --niov;
            } else {
                iov[0].iov_base = static_cast<char*>(iov[0].iov_base) + rem;
                iov[0].iov_len -= rem;
                rem = 0;
            }
        }
    }
    return true;
}

IpcBackend IpcBackend::connect(std::string_view path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return {};
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.data(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd); return {};
    }
    return IpcBackend(fd);
}

IpcBackend IpcBackend::listen_and_accept(std::string_view path) {
    int srv = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) return {};
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.data(), sizeof(addr.sun_path) - 1);
    ::unlink(addr.sun_path);
    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(srv, 1) < 0) {
        ::close(srv); return {};
    }
    int client = ::accept(srv, nullptr, nullptr);
    ::close(srv);
    return client >= 0 ? IpcBackend(client) : IpcBackend{};
}

void IpcBackend::emit(IrisValue&& v) {
    if (fd_ < 0 || !v.is_raw()) return;
    const auto& raw  = v.raw();
    uint64_t type_id = static_cast<uint64_t>(v.type_id);
    uint32_t sz      = static_cast<uint32_t>(raw.size());
    // Scatter-gather: header + payload in one writev syscall — no intermediate copy.
    struct iovec iov[3];
    iov[0] = { &type_id,                                sizeof(type_id) };
    iov[1] = { &sz,                                     sizeof(sz)      };
    iov[2] = { const_cast<std::byte*>(raw.data()),      sz              };
    writev_all(fd_, iov, (sz > 0) ? 3 : 2);
}

IrisValue IpcBackend::recv() {
    if (fd_ < 0) return {};
    uint64_t type_id = 0;
    uint32_t size    = 0;
    if (!read_all(&type_id, 8) || !read_all(&size, 4)) return {};
    // Reject frames that would cause a gigantic allocation — 64 MiB is
    // well above any real struct payload; a larger value is malformed input.
    static constexpr uint32_t kMaxFrameSize = 64u * 1024u * 1024u;
    if (size > kMaxFrameSize) return {};
    auto payload = IrisBuffer::alloc(size);
    if (size > 0 && !read_all(payload.data(), size)) return {};
    IrisValue v;
    v.type_id = static_cast<TypeId>(type_id);
    v.payload = std::move(payload);
    return v;
}

void IpcBackend::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

} // namespace iris
