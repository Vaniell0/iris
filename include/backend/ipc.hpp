/// @file   include/backend/ipc.hpp
/// @brief  IpcBackend — carries IrisValue over a Unix socket.
///
/// Wire format per message: [type_id: uint64_t LE][size: uint32_t LE][payload: size bytes]
/// Any-language peer that speaks this framing is a valid worker — no dlopen, no FFI.

#pragma once

#include <backend.hpp>
#include <channel.hpp>
#include <value.hpp>
#include <string_view>
#include <cstdint>

namespace iris {

class IpcBackend {
    int fd_ = -1;

    explicit IpcBackend(int fd) noexcept : fd_(fd) {}

public:
    IpcBackend() = default;
    ~IpcBackend() { close(); }

    IpcBackend(const IpcBackend&) = delete;
    IpcBackend& operator=(const IpcBackend&) = delete;
    IpcBackend(IpcBackend&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    IpcBackend& operator=(IpcBackend&& o) noexcept {
        if (this != &o) { close(); fd_ = o.fd_; o.fd_ = -1; }
        return *this;
    }

    static IpcBackend from_fd(int fd) { return IpcBackend(fd); }
    static IpcBackend connect(std::string_view path);
    static IpcBackend listen_and_accept(std::string_view path);

    std::string_view runtime_name() const { return "ipc"; }
    bool can_handle(const TypeDescriptor&) const { return true; }
    void emit(IrisValue&& v);
    IrisValue recv();

    bool connected() const noexcept { return fd_ >= 0; }
    void close();

    void set_input(Channel* ch)  { in_  = ch; }
    void set_output(Channel* ch) { out_ = ch; }

private:
    Channel* in_  = nullptr;
    Channel* out_ = nullptr;

    bool write_all(const void* buf, std::size_t n);
    bool read_all(void* buf, std::size_t n);
};

static_assert(Backend<IpcBackend>);

} // namespace iris
