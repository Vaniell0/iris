/// @file   include/backend/os.hpp
/// @brief  OsBackend — OS command source + ls/ps/env API.
///
/// OsBackend satisfies the Backend concept and streams typed OS data
/// (DirEntry, ProcEntry, EnvEntry) through recv(). emit() is a no-op.

#pragma once

#include <backend.hpp>
#include <sdk/cpp/os.hpp>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace iris::os {

/// Failure reason returned by OS command functions.
enum class OsError {
    NotFound,         ///< path or resource does not exist
    PermissionDenied, ///< insufficient privileges
    ReadError,        ///< underlying read failed
};

/// Walk @p path and return one DirEntry per visible entry.
std::expected<std::vector<IrisValue>, OsError> ls(std::string_view path = ".");
/// Read /proc and return one ProcEntry per running process.
std::expected<std::vector<IrisValue>, OsError> ps();
/// Return one EnvEntry per process environment variable.
std::expected<std::vector<IrisValue>, OsError> env();

} // namespace iris::os

namespace iris {

class OsBackend {
public:
    enum class Command { Ls, Ps, Env };

private:
    Command                command_;
    std::string            path_;
    std::vector<IrisValue> buffer_;
    size_t                 cursor_ = 0;
    bool                   loaded_ = false;

    void load() {
        if (loaded_) return;
        loaded_ = true;
        switch (command_) {
            case Command::Ls:  if (auto r = iris::os::ls(path_)) buffer_ = std::move(*r); break;
            case Command::Ps:  if (auto r = iris::os::ps())      buffer_ = std::move(*r); break;
            case Command::Env: if (auto r = iris::os::env())     buffer_ = std::move(*r); break;
        }
    }

    explicit OsBackend(Command cmd, std::string path = "")
        : command_(cmd), path_(std::move(path)) {}

public:
    static OsBackend ls(std::string_view path = ".") { return OsBackend(Command::Ls, std::string(path)); }
    static OsBackend ps()  { return OsBackend(Command::Ps);  }
    static OsBackend env() { return OsBackend(Command::Env); }

    std::string_view runtime_name() const { return "os"; }
    bool can_handle(const TypeDescriptor& td) const {
        return td.name == "DirEntry" || td.name == "ProcEntry" || td.name == "EnvEntry";
    }
    void emit(IrisValue&&) {}
    IrisValue recv() { load(); if (cursor_ >= buffer_.size()) return {}; return std::move(buffer_[cursor_++]); }
};

static_assert(Backend<OsBackend>);

} // namespace iris
