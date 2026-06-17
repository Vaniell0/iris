/// @file   include/os_backend.hpp
/// @brief  OsBackend — OS command source satisfying the Backend concept.
///
/// A streaming backend that buffers one OS command's output and feeds it
/// one IrisValue at a time through recv(). emit() is a no-op — OsBackend
/// is a source, not a sink. Returns a default IrisValue{} when exhausted.

#pragma once

#include <backend.hpp>
#include <os.hpp>

namespace iris {

class OsBackend {
public:
    enum class Command { Ls, Ps, Env };

private:
    Command                command_;
    std::string            path_;       ///< used only for Ls
    std::vector<IrisValue> buffer_;
    size_t                 cursor_ = 0;
    bool                   loaded_ = false;

    void load() {
        if (loaded_) return;
        loaded_ = true;
        switch (command_) {
            case Command::Ls: {
                if (auto r = iris::os::ls(path_)) buffer_ = std::move(*r);
                break;
            }
            case Command::Ps: {
                if (auto r = iris::os::ps())       buffer_ = std::move(*r);
                break;
            }
            case Command::Env: {
                if (auto r = iris::os::env())      buffer_ = std::move(*r);
                break;
            }
        }
    }

    explicit OsBackend(Command cmd, std::string path = "")
        : command_(cmd), path_(std::move(path)) {}

public:
    /// Construct an OsBackend that streams directory entries for @p path.
    static OsBackend ls(std::string_view path = ".") {
        return OsBackend(Command::Ls, std::string(path));
    }
    /// Construct an OsBackend that streams process entries from /proc.
    static OsBackend ps()  { return OsBackend(Command::Ps);  }
    /// Construct an OsBackend that streams environment variable entries.
    static OsBackend env() { return OsBackend(Command::Env); }

    // ── Backend concept ───────────────────────────────────────────────────────

    std::string_view runtime_name() const { return "os"; }

    bool can_handle(const TypeDescriptor& td) const {
        return td.name == "DirEntry" ||
               td.name == "ProcEntry" ||
               td.name == "EnvEntry";
    }

    /// Source backend — emit is a no-op.
    void emit(IrisValue&&) {}

    /// Return the next buffered entry, loading on first call.
    /// Returns IrisValue{} (type_id == 0) when the stream is exhausted.
    IrisValue recv() {
        load();
        if (cursor_ >= buffer_.size()) return {};
        return std::move(buffer_[cursor_++]);
    }
};

static_assert(Backend<OsBackend>);

} // namespace iris
